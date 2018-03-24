#include <exception>
#include <functional>
#include <string>

#include "core/common/logging/capture.h"
#include "core/common/logging/isink.h"
#include "core/common/logging/logging.h"

#include "test/common/logging/helpers.h"

using namespace Lotus::Logging;

// if we pull in the whole 'testing' namespace we get warnings from date.h as both use '_' in places.
// to avoid that we explicitly pull in the pieces we are using
using testing::Eq;
using testing::Field;
using testing::Ge;
using testing::HasSubstr;
using testing::Property;

/// <summary>
/// Tests that the WHERE macro populates all fields correctly.
/// </summary>
TEST(LoggingTests, TestWhereMacro) {
  const std::string logid{"TestWhereMacro"};
  const std::string message{"Testing the WHERE macro."};
  const Severity minLogLevel = Severity::kVERBOSE;
  const Severity logLevel = Severity::kERROR;

  const std::string file = __FILE__;
  const std::string function = __PRETTY_FUNCTION__;
  int log_line = 0;

  std::cout << function << std::endl;

  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, HasSubstr(logid),
                                 Property(&Capture::Location,
                                          AllOf(Field(&Location::line_num, Eq(std::ref(log_line))),
                                                Field(&Location::file_and_path, HasSubstr("lotus")),            // path
                                                Field(&Location::file_and_path, HasSubstr("logging_test.cc")),  // filename
                                                Field(&Location::function, HasSubstr("TestWhereMacro"))))))
      .WillRepeatedly(PrintArgs());

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), minLogLevel, false, "default"};

  std::unique_ptr<Logger> logger = manager.CreateLogger(logid);

  log_line = __LINE__ + 1;
  LOGS(logger.get(), ERROR, Category::Lotus) << message;
}

/// <summary>
/// Tests that the logging manager filters based on severity and user data correctly.
/// </summary>
TEST(LoggingTests, TestFiltering) {
  const std::string logid{"TestFiltering"};
  const Severity minLogLevel = Severity::kWARNING;
  const bool filter_user_data = true;

  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, HasSubstr(logid), Property(&Capture::Severity, Ge(minLogLevel))))
      .Times(1)
      .WillRepeatedly(PrintArgs());

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), minLogLevel, filter_user_data, "default"};

  auto logger = manager.CreateLogger(logid);
  auto loggerPtr = logger.get();

  LOGS(loggerPtr, VERBOSE, Category::Lotus) << "Verbose";      // filtered by Severity
  LOGS(loggerPtr, WARNING, "ArbitraryCategory") << "Warning";  // not filtered
  LOGS_USER(loggerPtr, ERROR, "PII") << "UserId:123";          // filtered due to DataType::UserData
}

/// <summary>
/// Tests that the logging manager constructor validates its usage correctly.
/// </summary>
TEST(LoggingTests, TestLoggingManagerCtor) {
  // must provide sink
  EXPECT_THROW((LoggingManager{std::unique_ptr<ISink>{nullptr}, Severity::kINFO, false, "default"}), std::logic_error);

  LoggingManager manager{std::unique_ptr<ISink>{new MockSink{}}, Severity::kINFO, false, "default"};
  // can't have two logging managers
  EXPECT_THROW((LoggingManager{std::unique_ptr<ISink>{new MockSink{}}, Severity::kINFO, false, "default"}), std::logic_error);
}

/// <summary>
/// Tests that the conditional logging macros work correctly.
/// </summary>
TEST(LoggingTests, TestConditionalMacros) {
  const std::string default_logger_id{"TestConditionalMacros.DefaultLogger"};
  const std::string logger_id{"TestConditionalMacros.NotDefaultLogger"};
  const Severity minLogLevel = Severity::kVERBOSE;
  const bool filter_user_data = true;

  MockSink *sinkPtr = new MockSink();

  // two logging calls that are true using default logger
  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, HasSubstr(default_logger_id), testing::_))
      .Times(2)
      .WillRepeatedly(PrintArgs());

  // two logging calls that are true using non-default logger
  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, HasSubstr(logger_id), testing::_))
      .Times(2)
      .WillRepeatedly(PrintArgs());

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), minLogLevel, filter_user_data, default_logger_id};

  auto logger = manager.CreateLogger(logger_id);
  auto loggerPtr = logger.get();

  LOGS_IF(loggerPtr == nullptr, loggerPtr, INFO, "Category:False") << "Null logger";            // false
  LOGS_IF(loggerPtr != nullptr, loggerPtr, INFO, "Category:True") << "Valid logger";            // true
  LOGF_IF(loggerPtr == nullptr, loggerPtr, INFO, "Category:False", "Logger is %p", loggerPtr);  // false
  LOGF_IF(loggerPtr != nullptr, loggerPtr, INFO, "Category:True", "Logger is %p", loggerPtr);   // true

  LOGS_DEFAULT_IF(loggerPtr == nullptr, INFO, "Category:False") << "Null logger";            // false
  LOGS_DEFAULT_IF(loggerPtr != nullptr, INFO, "Category:True") << "Valid logger";            // true
  LOGF_DEFAULT_IF(loggerPtr == nullptr, INFO, "Category:False", "Logger is %p", loggerPtr);  // false
  LOGF_DEFAULT_IF(loggerPtr != nullptr, INFO, "Category:True", "Logger is %p", loggerPtr);   // true
}

/// <summary>
/// Tests that the VLOG* macros produce the expected output.
/// Disabled in Release build, so should be no calls to SendImpl in that case.
/// </summary>
TEST(LoggingTests, TestVLog) {
  const std::string logid{"TestVLog"};

  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, HasSubstr(logid), testing::_))
#ifdef _DEBUG
      .Times(2)
      .WillRepeatedly(PrintArgs());
#else
      .Times(0);
#endif

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), Severity::kVERBOSE, false, logid};

  VLOGS(1) << "Stream";
  VLOGF(2, "Printf %d", 1);

#ifdef _DEBUG
  max_vlog_level = INT_MIN;
  VLOGS(0) << "Should be ignored.";
#endif
}

/// <summary>
/// Tests the *kFatal* macros.
/// </summary>
TEST(LoggingTests, TestFatalMacros) {
  const std::string logid{"TestFatalMacros"};

  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, testing::_, testing::_))
      .Times(2)
      .WillRepeatedly(PrintArgs());

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), Severity::kVERBOSE, false, logid};

  EXPECT_THROW(LOGF_FATAL("Category:Test", "Severity::Fatal should throw"), std::runtime_error);
  EXPECT_THROW(FATAL_IF(logid.length() == 15), std::runtime_error);
}

TEST(LoggingTests, TestNotNull) {
  const std::string logid{"NotNull"};

  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, testing::_, testing::_))
      .Times(5)  // 5 failed checks, 2 succeed so no logging
      .WillRepeatedly(PrintArgs());

  LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), Severity::kVERBOSE, false, logid};

  // test smart and raw pointers
  std::shared_ptr<std::string> strptr1;
  std::unique_ptr<std::string> strptr2;
  std::string *strptr3 = nullptr;

  EXPECT_THROW(CHECK_NOTNULL(strptr1), std::runtime_error);
  EXPECT_THROW(CHECK_NOTNULL(strptr2), std::runtime_error);
  EXPECT_THROW(CHECK_NOTNULL(strptr3), std::runtime_error);

  // validate some CHECK_OP based tests
  EXPECT_THROW(CHECK_EQ(1, 2), std::runtime_error);
  EXPECT_THROW(CHECK_GE(1, 2), std::runtime_error);
  EXPECT_NO_THROW(CHECK_NE(1, 2));
  EXPECT_NO_THROW(CHECK_LT(1, 2));
}

TEST(LoggingTests, LogCallAfterLoggingManagerDestroyed) {
  MockSink *sinkPtr = new MockSink();

  EXPECT_CALL(*sinkPtr, SendImpl(testing::_, testing::_, testing::_))
      .Times(0);

  std::unique_ptr<Logger> orphaned_logger_;

  {
    LoggingManager manager{std::unique_ptr<ISink>(sinkPtr), Severity::kVERBOSE, false, "default"};
    orphaned_logger_ = manager.CreateLogger("logger");
  }

  EXPECT_NO_THROW(LOGF(orphaned_logger_.get(), ERROR, "Test", "Please don't blow up"));
}
