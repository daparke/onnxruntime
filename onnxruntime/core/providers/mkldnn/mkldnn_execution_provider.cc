#include "mkldnn_allocator.h"
#include "mkldnn_execution_provider.h"
#include "core/framework/allocator.h"
#include "core/framework/transformer_memcpy.h"
#include "core/framework/memcpy.h"
#include "core/framework/kernel_registry.h"

namespace onnxruntime {
namespace mkl_dnn {

ONNX_OPERATOR_KERNEL_EX(
    MemcpyFromHost,
    kOnnxDomain,
    1,
    kMklDnnExecutionProvider,
    KernelDefBuilder().InputMemoryType<kMemTypeCPUInput>(0).TypeConstraint("T", DataTypeImpl::AllTensorTypes()),
    Memcpy);

ONNX_OPERATOR_KERNEL_EX(
    MemcpyToHost,
    kOnnxDomain,
    1,
    kMklDnnExecutionProvider,
    KernelDefBuilder().OutputMemoryType<kMemTypeCPUOutput>(0).TypeConstraint("T", DataTypeImpl::AllTensorTypes()),
    Memcpy);

}  // namespace mkl_dnn

MKLDNNExecutionProvider::MKLDNNExecutionProvider(const MKLDNNExecutionProviderInfo& /*info*/) {
  DeviceAllocatorRegistrationInfo default_allocator_info({kMemTypeDefault,
                                                          [](int) { return std::make_unique<MKLDNNAllocator>(); }, std::numeric_limits<size_t>::max()});
  InsertAllocator(kMemTypeDefault, CreateAllocator(default_allocator_info));

  DeviceAllocatorRegistrationInfo cpu_allocator_info({kMemTypeCPUOutput,
                                                      [](int) { return std::make_unique<MKLDNNCPUAllocator>(); }, std::numeric_limits<size_t>::max()});
  InsertAllocator(kMemTypeCPUOutput, CreateAllocator(cpu_allocator_info));
}

MKLDNNExecutionProvider::~MKLDNNExecutionProvider() {
}

Status MKLDNNExecutionProvider::CopyTensor(const Tensor& src, Tensor& dst) const {
  // Support CPU <-> MKLDNN for now
  if (!(strcmp(src.Location().name, MKLDNN) == 0 && strcmp(dst.Location().name, CPU) == 0) &&
      !(strcmp(src.Location().name, CPU) == 0 && strcmp(dst.Location().name, MKLDNN) == 0) &&
      !(strcmp(src.Location().name, MKLDNN) == 0 && strcmp(dst.Location().name, MKLDNN_CPU) == 0)) {
    LOTUS_NOT_IMPLEMENTED(src.Location().name, " copy to ", dst.Location().name, " is not implemented");
  }

  // Todo: Copy for now. May optimize later to avoid copy.
  size_t bytes = src.DataType()->Size() * src.Shape().Size();
  const void* src_data = src.DataRaw();
  void* dst_data = dst.MutableDataRaw();
  memcpy(dst_data, src_data, bytes);

  return Status::OK();
}

namespace mkl_dnn {
class ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, Conv);
class ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 7, Gemm);
class ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, MemcpyFromHost);
class ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, MemcpyToHost);
class ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 7, float, AveragePool);
class ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, GlobalAveragePool);
class ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, MaxPool);
class ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 8, float, MaxPool);
class ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, GlobalMaxPool);

void RegisterMKLDNNKernels(std::function<void(KernelCreateInfo&&)> fn) {
  fn(BuildKernel<ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, Conv)>());
  fn(BuildKernel<ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 7, Gemm)>());
  fn(BuildKernel<ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, MemcpyFromHost)>());
  fn(BuildKernel<ONNX_OPERATOR_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, MemcpyToHost)>());
  // the perf of mkldnn pooling is slower than our cpu implementation, so disablbe them now.
  //fn(BuildKernel<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 7, float, AveragePool)>());
  //fn(BuildKernel<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, GlobalAveragePool)>());
  //fn(BuildKernel<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, MaxPool)>());
  //fn(BuildKernel<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 8, float, MaxPool)>());
  //fn(BuildKernel<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME(kMklDnnExecutionProvider, kOnnxDomain, 1, float, GlobalMaxPool)>());
}
}  // namespace mkl_dnn

std::shared_ptr<KernelRegistry> MKLDNNExecutionProvider::GetKernelRegistry() const {
  static std::shared_ptr<KernelRegistry> kernel_registry = std::make_shared<KernelRegistry>(onnxruntime::mkl_dnn::RegisterMKLDNNKernels);
  return kernel_registry;
}
}  // namespace onnxruntime