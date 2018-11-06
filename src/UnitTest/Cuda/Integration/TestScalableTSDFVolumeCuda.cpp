//
// Created by wei on 10/20/18.
//

#include <Cuda/Integration/ScalableTSDFVolumeCuda.h>
#include <Cuda/Geometry/ImageCuda.h>
#include <Core/Core.h>
#include <opencv2/opencv.hpp>
#include "UnitTest.h"

TEST(ScalableTSDFVolumeCuda, Create) {
    using namespace open3d;

    ScalableTSDFVolumeCuda<8> volume;
    volume.Create(10000, 200000);

    ScalableTSDFVolumeCuda<8> volume_copy;
    volume_copy = volume;
}

TEST(ScalableTSDFVolumeCuda, TouchSubvolumes) {
    using namespace open3d;
    cv::Mat depth = cv::imread(
        "../../examples/TestData/RGBD/depth/00000.png",
        cv::IMREAD_UNCHANGED);
    cv::Mat color = cv::imread(
        "../../examples/TestData/RGBD/color/00000.jpg");
    cv::cvtColor(color, color, cv::COLOR_BGR2RGB);

    RGBDImageCuda rgbd(0.1f, 3.5f, 1000.0f);
    rgbd.Upload(depth, color);

    MonoPinholeCameraCuda intrinsics;
    intrinsics.SetUp();

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    extrinsics(0, 3) = 10.0f;
    extrinsics(1, 3) = -10.0f;
    extrinsics(2, 3) = 1.0f;
    ScalableTSDFVolumeCuda<8> volume(10000, 200000,
                                     voxel_length, 3 * voxel_length,
                                     extrinsics);

    volume.TouchSubvolumes(rgbd.depth(), intrinsics, extrinsics);
    volume.GetSubvolumesInFrustum(intrinsics, extrinsics);

    auto entry_vector = volume.active_subvolume_entry_array().Download();
    for (auto &entry : entry_vector) {
        PrintInfo("%d %d %d %d\n", entry.key(0), entry.key(1), entry.key(2),
            entry.internal_addr);
    }
}

TEST(ScalableTSDFVolumeCuda, Integration) {
    using namespace open3d;
    cv::Mat depth = cv::imread(
        "../../examples/TestData/RGBD/depth/00000.png",
        cv::IMREAD_UNCHANGED);
    cv::Mat color = cv::imread(
        "../../examples/TestData/RGBD/color/00000.jpg");
    cv::cvtColor(color, color, cv::COLOR_BGR2RGB);

    RGBDImageCuda rgbd(0.1f, 3.5f, 1000.0f);
    rgbd.Upload(depth, color);

    MonoPinholeCameraCuda intrinsics;
    intrinsics.SetUp();

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    ScalableTSDFVolumeCuda<8> volume(10000, 200000,
                                     voxel_length, 3 * voxel_length,
                                     extrinsics);
    Timer timer;
    timer.Start();
    for (int i = 0; i < 10; ++i) {
        volume.Integrate(rgbd, intrinsics, extrinsics);
    }
    timer.Stop();
    PrintInfo("Integrations takes %f milliseconds\n", timer.GetDuration() / 10);

    PrintInfo("Downloading volumes: \n");
    auto result = volume.DownloadVolumes();
    auto &keys = std::get<0>(result);
    auto &volumes = std::get<1>(result);
    for (int i = 0; i < keys.size(); ++i) {
        float sum_tsdf = 0;
        auto &volume = volumes[i];
        auto &tsdf = std::get<0>(volume);
        for (int k = 0; k < 512; ++k) {
            sum_tsdf += fabsf(tsdf[k]);
        }
        PrintInfo("%d %d %d %f\n", keys[i](0), keys[i](1), keys[i](2), sum_tsdf);
    }
}

TEST(ScalableTSDFVolumeCuda, RayCasting) {
    using namespace open3d;
    cv::Mat depth = cv::imread(
        "../../examples/TestData/RGBD/depth/00000.png",
        cv::IMREAD_UNCHANGED);
    cv::Mat color = cv::imread(
        "../../examples/TestData/RGBD/color/00000.jpg");
    cv::cvtColor(color, color, cv::COLOR_BGR2RGB);

    RGBDImageCuda rgbd(0.1f, 3.5f, 1000.0f);
    rgbd.Upload(depth, color);

    MonoPinholeCameraCuda intrinsics;
    intrinsics.SetUp();

    float voxel_length = 0.01f;
    TransformCuda extrinsics = TransformCuda::Identity();
    extrinsics(0, 3) = 10.0f;
    extrinsics(1, 3) = -10.0f;
    extrinsics(2, 3) = 1.0f;

    ScalableTSDFVolumeCuda<8> volume(10000, 200000,
                                     voxel_length, 3 * voxel_length,
                                     extrinsics);

    ImageCuda<Vector3f> raycaster(depth.cols, depth.rows);

    Timer timer;
    const int iters = 10;
    float time = 0;
    for (int i = 0; i < iters; ++i) {
        volume.Integrate(rgbd, intrinsics, extrinsics);
        timer.Start();
        volume.RayCasting(raycaster, intrinsics, extrinsics);
        timer.Stop();
        time += timer.GetDuration();

        cv::imshow("Raycaster", raycaster.Download());
        cv::waitKey(10);
    }
    cv::waitKey(-1);
    PrintInfo("Raycasting takes %f milliseconds\n", time / iters);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}