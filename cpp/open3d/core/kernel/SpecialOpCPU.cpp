// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/core/kernel/CPULauncher.h"
#include "open3d/core/kernel/SpecialOp.h"

namespace open3d {
namespace core {
namespace kernel {

void CPUIntegrateKernel(void* tsdf, const void* depth, float zc) {
    if (tsdf != nullptr && depth != nullptr) {
        *static_cast<float*>(tsdf) = (*static_cast<const float*>(depth) - zc);
    }
}

void SpecialOpEWCPU(SparseTensorList& sparse_tensor,
                    const std::vector<Tensor>& inputs,
                    const Projector& projector,
                    SpecialOpCode op_code) {
    switch (op_code) {
        case SpecialOpCode::Integrate: {
            // inputs[0]: depth
            // inputs[1]: intrinsic
            // inputs[2]: pose
            SparseIndexer indexer(sparse_tensor, {inputs[0]});
            CPULauncher::LaunchIntegrateKernel(
                    indexer, projector,
                    [=](void* tsdf, const void* depth, float zc) {
                        CPUIntegrateKernel(tsdf, depth, zc);
                    });
            utility::LogInfo("[SpecialOpEWCPU] CPULauncher finished");
            break;
        };
        default: { utility::LogError("Unsupported special op"); }
    }
    utility::LogInfo("[SpecialOpEWCPU] Exiting SpecialOpEWCPU");
}
}  // namespace kernel
}  // namespace core
}  // namespace open3d