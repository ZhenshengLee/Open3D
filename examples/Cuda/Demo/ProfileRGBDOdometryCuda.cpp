//
// Created by wei on 11/14/18.
//

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <Core/Core.h>
#include <IO/IO.h>
#include <Cuda/Odometry/RGBDOdometryCuda.h>
#include <Cuda/Integration/ScalableTSDFVolumeCuda.h>
#include <Cuda/Integration/ScalableMeshVolumeCuda.h>
#include <Cuda/Geometry/PointCloudCuda.h>
#include <Visualization/Visualization.h>

#include <opencv2/opencv.hpp>
#include <thread>
#include "examples/Cuda/Utils.h"

using namespace open3d;

void PrintHelp() {
    PrintOpen3DVersion();
    PrintInfo("Usage :\n");
    PrintInfo("    > ProfileRGBDOdometryCuda [dataset_path]\n");
}

void WriteLossesToLog(
    std::ofstream &fout,
    int frame_idx,
    std::vector<std::vector<float>> &losses) {
    assert(fout.is_open());

    fout << frame_idx << "\n";
    for (auto &losses_on_level : losses) {
        for (auto &loss : losses_on_level) {
            fout << loss << " ";
        }
        fout << "\n";
    }
}

int main(int argc, char **argv) {
    if (argc == 1 || ProgramOptionExists(argc, argv, "--help")) {
        PrintHelp();
        return 1;
    }

    /** Load data **/
    std::string base_path = argv[1];
    Image source_color, source_depth, target_color, target_depth;
    auto rgbd_filenames = ReadDataAssociation(
        base_path + "/data_association.txt");

    PinholeCameraTrajectory trajectory;

    /** Prepare odometry **/
    cuda::RGBDOdometryCuda<3> odometry;
    odometry.SetIntrinsics(PinholeCameraIntrinsic(
        PinholeCameraIntrinsicParameters::PrimeSenseDefault));
    odometry.SetParameters(OdometryOption({60, 60, 60}, 0.003, 0.01, 4.0), 0.5f);

    for (int step = 1; step < 2; ++step) {
        std::string log_filename =
            "odometry_less_assoc_step_" + std::to_string(step) + ".log";
        std::ofstream fout(log_filename);
        if (!fout.is_open()) {
            PrintError("Unable to write to log file %s, abort.\n",
                       log_filename.c_str());
        }

        PrintInfo("Step: %d\n", step);
        for (int i = 0; i + step < rgbd_filenames.size(); ++i) {
            PrintInfo("%d\n", i);
            std::stringstream ss;

            ReadImage(base_path + "/" + rgbd_filenames[i].second,
                      target_color);
            ReadImage(base_path + "/" + rgbd_filenames[i].first,
                      target_depth);
            ReadImage(base_path + "/" + rgbd_filenames[i + step].second,
                      source_color);
            ReadImage(base_path + "/" + rgbd_filenames[i + step].first,
                      source_depth);

            cuda::RGBDImageCuda source(0.1f, 4.0f, 1000.0f),
                target(0.1f, 4.0f, 1000.0f);
            source.Upload(source_depth, source_color);
            target.Upload(target_depth, target_color);

            odometry.Initialize(source, target);
            odometry.transform_source_to_target_ = Eigen::Matrix4d::Identity();

            auto result = odometry.ComputeMultiScale();
            if (std::get<0>(result)) {
                WriteLossesToLog(fout, i, std::get<2>(result));
            }
        }
    }

    return 0;
}