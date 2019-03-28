//
// Created by wei on 10/20/18.
//

#pragma once
#include "ScalableTSDFVolumeCudaDevice.cuh"

namespace open3d {
namespace cuda {

template<size_t N>
__global__
void CreateKernel(ScalableTSDFVolumeCudaDevice<N> server) {
    const size_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= server.value_capacity_) return;

    const size_t offset = (N * N * N) * index;
    UniformTSDFVolumeCudaDevice < N > &subvolume = server.hash_table_
        .memory_heap_value_.value_at(index);

    /** Assign memory **/
    subvolume.tsdf_ = &server.tsdf_memory_pool_[offset];
    subvolume.weight_ = &server.weight_memory_pool_[offset];
    subvolume.color_ = &server.color_memory_pool_[offset];

    /** Assign property **/
    subvolume.voxel_length_ = server.voxel_length_;
    subvolume.inv_voxel_length_ = server.inv_voxel_length_;
    subvolume.sdf_trunc_ = server.sdf_trunc_;
    subvolume.transform_volume_to_world_ = server.transform_volume_to_world_;
    subvolume.transform_world_to_volume_ = server.transform_world_to_volume_;
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::Create(
    ScalableTSDFVolumeCuda<N> &volume) {

    const dim3 threads(THREAD_1D_UNIT);
    const dim3 blocks(DIV_CEILING(volume.value_capacity_, THREAD_1D_UNIT));
    CreateKernel << < blocks, threads >> > (*volume.device_);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}

template<size_t N>
__global__
void TouchSubvolumesKernel(ScalableTSDFVolumeCudaDevice<N> server,
                           ImageCudaDevice<float, 1> depth,
                           PinholeCameraIntrinsicCuda camera,
                           TransformCuda transform_camera_to_world) {
    const int x = threadIdx.x + blockIdx.x * blockDim.x;
    const int y = threadIdx.y + blockIdx.y * blockDim.y;

    if (x >= depth.width_ || y >= depth.height_) return;

    const Vector2i p = Vector2i(x, y);
    server.TouchSubvolume(p, depth, camera, transform_camera_to_world);
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::TouchSubvolumes(
    ScalableTSDFVolumeCuda<N> &volume,
    ImageCuda<float, 1> &depth,
    PinholeCameraIntrinsicCuda &camera,
    TransformCuda &transform_camera_to_world) {

    const dim3 blocks(DIV_CEILING(depth.width_, THREAD_2D_UNIT),
                      DIV_CEILING(depth.height_, THREAD_2D_UNIT));
    const dim3 threads(THREAD_2D_UNIT, THREAD_2D_UNIT);
    TouchSubvolumesKernel << < blocks, threads >> > (
        *volume.device_, *depth.device_, camera, transform_camera_to_world);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}

template<size_t N>
__global__
void IntegrateSubvolumesKernel(ScalableTSDFVolumeCudaDevice<N> server,
                               RGBDImageCudaDevice rgbd,
                               PinholeCameraIntrinsicCuda camera,
                               TransformCuda transform_camera_to_world) {

    const size_t entry_idx = blockIdx.x;
    const Vector3i Xlocal = Vector3i(threadIdx.x, threadIdx.y, threadIdx.z);

#ifdef CUDA_DEBUG_ENABLE_ASSERTION
    assert(entry_idx < server.active_subvolume_entry_array().size()
        && Xlocal(0) < N && Xlocal(1) < N && Xlocal(2) < N);
#endif

    HashEntry<Vector3i>
        &entry = server.active_subvolume_entry_array_.at(entry_idx);
#ifdef CUDA_DEBUG_ENABLE_ASSERTION
    assert(entry.internal_addr >= 0);
#endif
    server.Integrate(Xlocal, entry, rgbd, camera, transform_camera_to_world);
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::IntegrateSubvolumes(
    ScalableTSDFVolumeCuda<N> &volume,
    RGBDImageCuda &rgbd,
    PinholeCameraIntrinsicCuda &camera,
    TransformCuda &transform_camera_to_world) {

    const dim3 blocks(volume.active_subvolume_entry_array().size());
    const dim3 threads(THREAD_3D_UNIT, THREAD_3D_UNIT, THREAD_3D_UNIT);
    IntegrateSubvolumesKernel << < blocks, threads >> > (
        *volume.device_, *rgbd.device_, camera, transform_camera_to_world);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}

template<size_t N>
__global__
void GetSubvolumesInFrustumKernel(ScalableTSDFVolumeCudaDevice<N> server,
                                  PinholeCameraIntrinsicCuda camera,
                                  TransformCuda transform_camera_to_world) {
    const int bucket_idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (bucket_idx >= server.bucket_count_) return;

    auto &hash_table = server.hash_table_;

    int bucket_base_idx = bucket_idx * BUCKET_SIZE;
#pragma unroll 1
    for (size_t i = 0; i < BUCKET_SIZE; ++i) {
        HashEntry<Vector3i> &entry = hash_table.entry_array_.at(
            bucket_base_idx + i);
        if (entry.internal_addr != NULLPTR_CUDA) {
            Vector3f
            X = server.voxelf_local_to_global(Vector3f(0), entry.key);
            if (camera.IsPointInFrustum(
                transform_camera_to_world.Inverse()
                    * server.voxelf_to_world(X))) {
                server.ActivateSubvolume(entry);
            }
        }
    }

    LinkedListCudaDevice<HashEntry<Vector3i>> &linked_list =
        hash_table.entry_list_array_.at(bucket_idx);
    int node_ptr = linked_list.head_node_ptr();
    while (node_ptr != NULLPTR_CUDA) {
        LinkedListNodeCuda<HashEntry<Vector3i>> &linked_list_node =
            linked_list.get_node(node_ptr);

        HashEntry<Vector3i> &entry = linked_list_node.data;
        Vector3f
        X = server.voxelf_local_to_global(Vector3f(0), entry.key);
        if (camera.IsPointInFrustum(
            transform_camera_to_world.Inverse() * server.voxelf_to_world(X))) {
            server.ActivateSubvolume(entry);
        }

        node_ptr = linked_list_node.next_node_ptr;
    }
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::GetSubvolumesInFrustum(
    ScalableTSDFVolumeCuda<N> &volume,
    PinholeCameraIntrinsicCuda &camera,
    TransformCuda &transform_camera_to_world) {

    const dim3 blocks(volume.bucket_count_);
    const dim3 threads(THREAD_1D_UNIT);
    GetSubvolumesInFrustumKernel << < blocks, threads >> > (
        *volume.device_, camera, transform_camera_to_world);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}

template<size_t N>
__global__
void GetAllSubvolumesKernel(ScalableTSDFVolumeCudaDevice<N> server) {
    const int bucket_idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (bucket_idx >= server.bucket_count_) return;

    auto &hash_table = server.hash_table_;

    int bucket_base_idx = bucket_idx * BUCKET_SIZE;
#pragma unroll 1
    for (size_t i = 0; i < BUCKET_SIZE; ++i) {
        HashEntry<Vector3i> &entry = hash_table.entry_array_.at(
            bucket_base_idx + i);
        if (entry.internal_addr != NULLPTR_CUDA) {
            server.ActivateSubvolume(entry);
        }
    }

    LinkedListCudaDevice<HashEntry<Vector3i>> &linked_list =
        hash_table.entry_list_array_.at(bucket_idx);
    int node_ptr = linked_list.head_node_ptr();
    while (node_ptr != NULLPTR_CUDA) {
        LinkedListNodeCuda<HashEntry<Vector3i>> &linked_list_node =
            linked_list.get_node(node_ptr);
        server.ActivateSubvolume(linked_list_node.data);
        node_ptr = linked_list_node.next_node_ptr;
    }
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::GetAllSubvolumes(
    ScalableTSDFVolumeCuda<N> &volume) {

    const dim3 blocks(volume.bucket_count_);
    const dim3 threads(THREAD_1D_UNIT);
    GetAllSubvolumesKernel << < blocks, threads >> > (*volume.device_);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}

template<size_t N>
__global__
void RayCastingKernel(ScalableTSDFVolumeCudaDevice<N> server,
                      ImageCudaDevice<float, 3> normal,
                      PinholeCameraIntrinsicCuda camera,
                      TransformCuda transform_camera_to_world) {
    const int x = threadIdx.x + blockIdx.x * blockDim.x;
    const int y = threadIdx.y + blockIdx.y * blockDim.y;

    if (x >= normal.width_ || y >= normal.height_) return;

    Vector2i p = Vector2i(x, y);
    Vector3f
    n = server.RayCasting(p, camera, transform_camera_to_world);
    normal.at(x, y) = (n == Vector3f::Zeros()) ? n : 0.5f * n + Vector3f(0.5f);
}

template<size_t N>
__host__
void ScalableTSDFVolumeCudaKernelCaller<N>::RayCasting(
    ScalableTSDFVolumeCuda<N> &volume,
    ImageCuda<float, 3> &image,
    PinholeCameraIntrinsicCuda &camera,
    TransformCuda &transform_camera_to_world) {
    const dim3 blocks(DIV_CEILING(image.width_, THREAD_2D_UNIT),
                      DIV_CEILING(image.height_, THREAD_2D_UNIT));
    const dim3 threads(THREAD_2D_UNIT, THREAD_2D_UNIT);
    RayCastingKernel << < blocks, threads >> > (
        *volume.device_, *image.device_, camera, transform_camera_to_world);
    CheckCuda(cudaDeviceSynchronize());
    CheckCuda(cudaGetLastError());
}
} // cuda
} // open3d