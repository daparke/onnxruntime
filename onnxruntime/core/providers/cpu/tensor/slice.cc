#include "core/providers/cpu/tensor/slice.h"
#include "core/providers/cpu/tensor/utils.h"
using namespace ::onnxruntime::common;

namespace onnxruntime {

ONNX_CPU_OPERATOR_KERNEL(
    Slice,
    1,
    KernelDefBuilder().TypeConstraint("T", DataTypeImpl::GetTensorType<float>()),
    Slice<float>);

// std::clamp doesn't exist until C++17 so create a local version
template <typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

template <>
Status Slice<float>::Compute(OpKernelContext* ctx) const {
  auto& input_tensor = *ctx->Input<Tensor>(0);
  auto& input_dimensions = input_tensor.Shape().GetDims();

  // Initialize the starts & ends to the actual tensor shape
  const size_t dimension_count = input_dimensions.size();
  std::vector<int64_t> starts(dimension_count, 0);
  std::vector<int64_t> output_dims(input_dimensions);

  // Initialize axes to the provided axes attribute or to the default sequence
  std::vector<size_t> axes;
  if (has_axes_) {
    axes.resize(axes_.size());
    std::copy(axes_.begin(), axes_.end(), axes.begin());
  } else {
    //axes are omitted, they are set to[0, ..., ndim - 1]
    axes.resize(starts.size());
    for (size_t i = 0; i < starts.size(); i++)
      axes[i] = i;
  }

  if (axes.size() > starts_.size())
    return Status(LOTUS, INVALID_ARGUMENT, "'axes' has more entries than the 'starts' attribute holds");
  if (axes.size() > ends_.size())
    return Status(LOTUS, INVALID_ARGUMENT, "'axes' has more entries than the 'ends' attribute holds");

  // Iterate through the provided axes and override the start/end ranges
  for (size_t axesIndex = 0; axesIndex < axes.size(); axesIndex++) {
    auto axis = axes[axesIndex];
    if (axis >= dimension_count)
      return Status(LOTUS, INVALID_ARGUMENT, "'axes' has an axis outside of the tensor dimension count");
    auto start = starts_[axesIndex];
    if (start < 0)
      start += input_dimensions[axis];
    starts[axis] = clamp(start, int64_t{0}, input_dimensions[axis]);

    auto end = ends_[axesIndex];
    if (end < 0)
      end += input_dimensions[axis];
    output_dims[axis] = clamp(end, int64_t{0}, input_dimensions[axis]) - starts[axis];
    if (output_dims[axis] < 0)
      return Status(LOTUS, INVALID_ARGUMENT, "'starts' and 'ends' values resulted in a negative dimension");
  }

  TensorShape output_shape(output_dims);
  auto& output_tensor = *ctx->Output(0, output_shape);
  auto* output = output_tensor.MutableData<float>();
  const auto* output_end = output + output_shape.Size();

  SliceIterator<float> input_iterator(input_tensor, starts, output_dims);
  while (output != output_end)
    *output++ = *input_iterator++;

  return Status::OK();
}

}  // namespace onnxruntime