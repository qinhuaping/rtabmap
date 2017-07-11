/*
Copyright (c) 2010-2016, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <rtabmap/core/OdometryF2M.h>
#include "rtabmap/core/Rtabmap.h"
#include "rtabmap/core/CameraStereo.h"
#include "rtabmap/core/CameraThread.h"
#include "rtabmap/core/Graph.h"
#include "rtabmap/core/OdometryInfo.h"
#include "rtabmap/core/OdometryEvent.h"
#include "rtabmap/core/Memory.h"
#include "rtabmap/core/util3d_registration.h"
#include "rtabmap/utilite/UConversion.h"
#include "rtabmap/utilite/UDirectory.h"
#include "rtabmap/utilite/UFile.h"
#include "rtabmap/utilite/UMath.h"
#include "rtabmap/utilite/UStl.h"
#include <pcl/common/common.h>
#include <stdio.h>
#include <signal.h>

using namespace rtabmap;

void showUsage()
{
	printf("\nUsage:\n"
			"rtabmap-kitti_dataset [options] path\n"
			"  path               Folder of the sequence (e.g., \"~/KITTI/dataset/sequences/07\")\n"
			"                        containing least calib.txt, times.txt, image_0 and image_1 folders.\n"
			"                        Optional image_2, image_3 and velodyne folders.\n"
			"  --output           Output directory. By default, results are saved in \"path\".\n"
			"  --gt \"path\"        Ground truth path (e.g., ~/KITTI/devkit/cpp/data/odometry/poses/07.txt)\n"
			"  --color            Use color images for stereo (image_2 and image_3 folders).\n"
			"  --disp             Generate full disparity.\n"
			"  --scan             Include velodyne scan in node's data.\n"
			"  --scan_step #      Scan downsample step (default=10).\n"
			"  --scan_voxel #.#   Scan voxel size (default 0.3 m).\n"
			"  --scan_k           Scan normal K (default 20).\n"
			"  --map_update  #    Do map update each X odometry frames (default=10, which\n"
			"                        gives 1 Hz map update assuming images are at 10 Hz).\n\n"
			"%s\n"
			"Example:\n\n"
			"   $ rtabmap-kitti_dataset \\\n"
			"       --Vis/EstimationType 1\\\n"
			"       --Vis/BundleAdjustment 1\\\n"
			"       --Vis/PnPReprojError 1.5\\\n"
			"       --Odom/GuessMotion true\\\n"
			"       --OdomF2M/BundleAdjustment 1\\\n"
			"       --Rtabmap/CreateIntermediateNodes true\\\n"
			"       --gt \"~/KITTI/devkit/cpp/data/odometry/poses/07.txt\"\\\n"
			"       ~/KITTI/dataset/sequences/07\n\n", rtabmap::Parameters::showUsage());
	exit(1);
}

// catch ctrl-c
bool g_forever = true;
void sighandler(int sig)
{
	printf("\nSignal %d caught...\n", sig);
	g_forever = false;
}

int main(int argc, char * argv[])
{
	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	ULogger::setType(ULogger::kTypeConsole);
	ULogger::setLevel(ULogger::kWarning);

	ParametersMap parameters;
	std::string path;
	std::string output;
	std::string seq;
	int mapUpdate = 10;
	bool color = false;
	bool scan = false;
	bool disp = false;
	int scanStep = 10;
	float scanVoxel = 0.3f;
	int scanNormalK = 20;
	std::string gtPath;
	if(argc < 2)
	{
		showUsage();
	}
	else
	{
		for(int i=1; i<argc; ++i)
		{
			if(std::strcmp(argv[i], "--output") == 0)
			{
				output = argv[++i];
			}
			else if(std::strcmp(argv[i], "--map_update") == 0)
			{
				mapUpdate = atoi(argv[++i]);
				if(mapUpdate <= 0)
				{
					printf("map_update should be > 0\n");
					showUsage();
				}
			}
			else if(std::strcmp(argv[i], "--scan_step") == 0)
			{
				scanStep = atoi(argv[++i]);
				if(scanStep <= 0)
				{
					printf("scan_step should be > 0\n");
					showUsage();
				}
			}
			else if(std::strcmp(argv[i], "--scan_voxel") == 0)
			{
				scanVoxel = atof(argv[++i]);
				if(scanVoxel < 0.0f)
				{
					printf("scan_voxel should be >= 0.0\n");
					showUsage();
				}
			}
			else if(std::strcmp(argv[i], "--scan_k") == 0)
			{
				scanNormalK = atoi(argv[++i]);
				if(scanNormalK < 0)
				{
					printf("scanNormalK should be >= 0\n");
					showUsage();
				}
			}
			else if(std::strcmp(argv[i], "--gt") == 0)
			{
				gtPath = argv[++i];
			}
			else if(std::strcmp(argv[i], "--color") == 0)
			{
				color = true;
			}
			else if(std::strcmp(argv[i], "--scan") == 0)
			{
				scan = true;
			}
			else if(std::strcmp(argv[i], "--disp") == 0)
			{
				disp = true;
			}
		}
		parameters = Parameters::parseArguments(argc, argv);
		path = argv[argc-1];
		path = uReplaceChar(path, '~', UDirectory::homeDir());
		path = uReplaceChar(path, '\\', '/');
		if(output.empty())
		{
			output = path;
		}
		else
		{
			output = uReplaceChar(output, '~', UDirectory::homeDir());
			UDirectory::makeDir(output);
		}
	}

	seq = uSplit(path, '/').back();
	if(seq.empty() || !(uStr2Int(seq)>=0 && uStr2Int(seq)<=21))
	{
		UWARN("Sequence number \"%s\" should be between 0 and 21 (official KITTI datasets).", seq.c_str());
		seq.clear();
	}
	std::string pathLeftImages  = path+(color?"/image_2":"/image_0");
	std::string pathRightImages = path+(color?"/image_3":"/image_1");
	std::string pathCalib = path+"/calib.txt";
	std::string pathTimes = path+"/times.txt";
	std::string pathScan;

	printf("Paths:\n"
			"   Sequence number:  %s\n"
			"   Sequence path:    %s\n"
			"   Output:           %s\n"
			"   left images:      %s\n"
			"   right images:     %s\n"
			"   calib.txt:        %s\n"
			"   times.txt:        %s\n",
			seq.c_str(),
			path.c_str(),
			output.c_str(),
			pathLeftImages.c_str(),
			pathRightImages.c_str(),
			pathCalib.c_str(),
			pathTimes.c_str());
	if(!gtPath.empty())
	{
		gtPath = uReplaceChar(gtPath, '~', UDirectory::homeDir());
		gtPath = uReplaceChar(gtPath, '\\', '/');
		if(!UFile::exists(gtPath))
		{
			UWARN("Ground truth file path doesn't exist: \"%s\", benchmark values won't be computed.", gtPath.c_str());
			gtPath.clear();
		}
		else
		{
			printf("   Ground Truth:      %s\n", gtPath.c_str());
		}
	}
	if(disp)
	{
		printf("   Disparity:         %s\n", disp?"true":"false");
	}
	if(scan)
	{
		pathScan = path+"/velodyne";
		printf("   Scan:              %s\n", pathScan.c_str());
		printf("   Scan step:         %d\n", scanStep);
		printf("   Scan voxel:        %fm\n", scanVoxel);
		printf("   Scan normal k:     %d\n", scanNormalK);
	}
	if(!parameters.empty())
	{
		printf("Parameters:\n");
		for(ParametersMap::iterator iter=parameters.begin(); iter!=parameters.end(); ++iter)
		{
			printf("   %s=%s\n", iter->first.c_str(), iter->second.c_str());
		}
	}

	// convert calib.txt to rtabmap format (yaml)
	FILE * pFile = 0;
	pFile = fopen(pathCalib.c_str(),"r");
	if(!pFile)
	{
		UERROR("Cannot open calibration file \"%s\"", pathCalib.c_str());
		return -1;
	}
	cv::Mat_<double> P0(3,4);
	cv::Mat_<double> P1(3,4);
	cv::Mat_<double> P2(3,4);
	cv::Mat_<double> P3(3,4);
	if(fscanf (pFile, "%*s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
			&P0(0, 0), &P0(0, 1), &P0(0, 2), &P0(0, 3),
			&P0(1, 0), &P0(1, 1), &P0(1, 2), &P0(1, 3),
			&P0(2, 0), &P0(2, 1), &P0(2, 2), &P0(2, 3)) != 12)
	{
		UERROR("Failed to parse calibration file \"%s\"", pathCalib.c_str());
		return -1;
	}
	if(fscanf (pFile, "%*s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
			&P1(0, 0), &P1(0, 1), &P1(0, 2), &P1(0, 3),
			&P1(1, 0), &P1(1, 1), &P1(1, 2), &P1(1, 3),
			&P1(2, 0), &P1(2, 1), &P1(2, 2), &P1(2, 3)) != 12)
	{
		UERROR("Failed to parse calibration file \"%s\"", pathCalib.c_str());
		return -1;
	}
	if(fscanf (pFile, "%*s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
			&P2(0, 0), &P2(0, 1), &P2(0, 2), &P2(0, 3),
			&P2(1, 0), &P2(1, 1), &P2(1, 2), &P2(1, 3),
			&P2(2, 0), &P2(2, 1), &P2(2, 2), &P2(2, 3)) != 12)
	{
		UERROR("Failed to parse calibration file \"%s\"", pathCalib.c_str());
		return -1;
	}
	if(fscanf (pFile, "%*s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
			&P3(0, 0), &P3(0, 1), &P3(0, 2), &P3(0, 3),
			&P3(1, 0), &P3(1, 1), &P3(1, 2), &P3(1, 3),
			&P3(2, 0), &P3(2, 1), &P3(2, 2), &P3(2, 3)) != 12)
	{
		UERROR("Failed to parse calibration file \"%s\"", pathCalib.c_str());
		return -1;
	}
	fclose (pFile);
	// get image size
	UDirectory dir(pathLeftImages);
	std::string firstImage = dir.getNextFileName();
	cv::Mat image = cv::imread(dir.getNextFilePath());
	if(image.empty())
	{
		UERROR("Failed to read first image of \"%s\"", firstImage.c_str());
		return -1;
	}
	StereoCameraModel model("rtabmap_calib"+seq,
			image.size(), P0.colRange(0,3), cv::Mat(), cv::Mat(), P0,
			image.size(), P1.colRange(0,3), cv::Mat(), cv::Mat(), P1,
			cv::Mat(), cv::Mat(), cv::Mat(), cv::Mat());
	if(!model.save(output, true))
	{
		UERROR("Could not save calibration!");
		return -1;
	}
	printf("Saved calibration \"%s\" to \"%s\"\n", ("rtabmap_calib"+seq).c_str(), output.c_str());

	// We use CameraThread only to use postUpdate() method
	Transform opticalRotation(0,0,1,0, -1,0,0,color?-0.06:0, 0,-1,0,0);
	CameraThread cameraThread(new
		CameraStereoImages(
				pathLeftImages,
				pathRightImages,
				false, // assume that images are already rectified
				0.0f,
				opticalRotation), parameters);
	((CameraStereoImages*)cameraThread.camera())->setTimestamps(false, pathTimes, false);
	if(disp)
	{
		cameraThread.setStereoToDepth(true);
	}
	if(!gtPath.empty())
	{
		((CameraStereoImages*)cameraThread.camera())->setGroundTruthPath(gtPath, 2);
	}
	if(!pathScan.empty())
	{
		((CameraStereoImages*)cameraThread.camera())->setScanPath(
						pathScan,
						130000,
						scanStep,
						scanVoxel,
						scanNormalK,
						Transform(-0.27f, 0.0f, 0.08, 0.0f, 0.0f, 0.0f));
	}

	bool intermediateNodes = Parameters::defaultRtabmapCreateIntermediateNodes();
	Parameters::parse(parameters, Parameters::kRtabmapCreateIntermediateNodes(), intermediateNodes);
	std::string databasePath = output+"/rtabmap" + seq + ".db";
	UFile::erase(databasePath);
	if(cameraThread.camera()->init(output, "rtabmap_calib"+seq))
	{
		int totalImages = (int)((CameraStereoImages*)cameraThread.camera())->filenames().size();

		OdometryF2M odom(parameters);
		Rtabmap rtabmap;
		rtabmap.init(parameters, databasePath);

		UTimer totalTime;
		UTimer timer;
		CameraInfo cameraInfo;
		SensorData data = cameraThread.camera()->takeImage(&cameraInfo);
		int iteration = 0;

		/////////////////////////////
		// Processing dataset begin
		/////////////////////////////
		cv::Mat covariance;
		while(data.isValid() && g_forever)
		{
			std::map<std::string, float> externalStats;
			cameraThread.postUpdate(&data, &cameraInfo);
			cameraInfo.timeTotal = timer.ticks();

			// save camera statistics to database
			externalStats.insert(std::make_pair("Camera/BilateralFiltering/ms", cameraInfo.timeBilateralFiltering*1000.0f));
			externalStats.insert(std::make_pair("Camera/Capture/ms", cameraInfo.timeCapture*1000.0f));
			externalStats.insert(std::make_pair("Camera/Disparity/ms", cameraInfo.timeDisparity*1000.0f));
			externalStats.insert(std::make_pair("Camera/ImageDecimation/ms", cameraInfo.timeImageDecimation*1000.0f));
			externalStats.insert(std::make_pair("Camera/Mirroring/ms", cameraInfo.timeMirroring*1000.0f));
			externalStats.insert(std::make_pair("Camera/ScanFromDepth/ms", cameraInfo.timeScanFromDepth*1000.0f));
			externalStats.insert(std::make_pair("Camera/TotalTime/ms", cameraInfo.timeTotal*1000.0f));
			externalStats.insert(std::make_pair("Camera/UndistortDepth/ms", cameraInfo.timeUndistortDepth*1000.0f));

			OdometryInfo odomInfo;
			Transform pose = odom.process(data, &odomInfo);
			externalStats.insert(std::make_pair("Odometry/LocalBundle/ms", odomInfo.localBundleTime*1000.0f));
			externalStats.insert(std::make_pair("Odometry/TotalTime/ms", odomInfo.timeEstimation*1000.0f));
			float speed = 0.0f;
			if(odomInfo.interval>0.0)
				speed = odomInfo.transform.x()/odomInfo.interval*3.6;
			externalStats.insert(std::make_pair("Odometry/Speed/kph", speed));
			externalStats.insert(std::make_pair("Odometry/Inliers/ms", odomInfo.inliers));
			externalStats.insert(std::make_pair("Odometry/Features/ms", odomInfo.features));

			bool processData = true;
			if(iteration % mapUpdate != 0)
			{
				// set negative id so rtabmap will detect it as an intermediate node
				data.setId(-1);
				data.setFeatures(std::vector<cv::KeyPoint>(), std::vector<cv::Point3f>(), cv::Mat());// remove features
				processData = intermediateNodes;
			}
			if(covariance.empty())
			{
				covariance = odomInfo.covariance;
			}
			else
			{
				covariance += odomInfo.covariance;
			}

			timer.restart();
			if(processData)
			{
				OdometryEvent e(SensorData(), Transform(), odomInfo);
				rtabmap.process(data, pose, covariance, e.velocity(), externalStats);
				covariance = cv::Mat();
			}
			double slamTime = timer.ticks();

			++iteration;
			printf("Iteration %d/%d: speed=%dkm/h camera=%dms, odom(quality=%d/%d)=%dms, slam=%dms",
					iteration, totalImages, int(speed), int(cameraInfo.timeTotal*1000.0f), odomInfo.inliers, odomInfo.features, int(odomInfo.timeEstimation*1000.0f), int(slamTime*1000.0f));
			if(processData && rtabmap.getLoopClosureId()>0)
			{
				printf(" *");
			}
			printf("\n");

			cameraInfo = CameraInfo();
			timer.restart();
			data = cameraThread.camera()->takeImage(&cameraInfo);
		}
		printf("Total time=%fs\n", totalTime.ticks());
		/////////////////////////////
		// Processing dataset end
		/////////////////////////////

		// Save trajectory
		printf("Saving rtabmap_trajectory.txt ...\n");
		std::map<int, Transform> poses;
		std::multimap<int, Link> links;
		rtabmap.getGraph(poses, links, true, true);
		std::string pathTrajectory = output+"/rtabmap_poses"+seq+".txt";
		if(poses.size() && graph::exportPoses(pathTrajectory, 2, poses, links))
		{
			printf("Saving %s... done!\n", pathTrajectory.c_str());
		}
		else
		{
			printf("Saving %s... failed!\n", pathTrajectory.c_str());
		}

		if(!gtPath.empty())
		{
			// Log ground truth statistics (in TUM's RGBD-SLAM format)
			std::map<int, Transform> groundTruth;

			//align with ground truth for more meaningful results
			pcl::PointCloud<pcl::PointXYZ> cloud1, cloud2;
			cloud1.resize(poses.size());
			cloud2.resize(poses.size());
			int oi = 0;
			int idFirst = 0;
			for(std::map<int, Transform>::const_iterator iter=poses.begin(); iter!=poses.end(); ++iter)
			{
				Transform o, gtPose;
				int m,w;
				std::string l;
				double s;
				std::vector<float> v;
				rtabmap.getMemory()->getNodeInfo(iter->first, o, m, w, l, s, gtPose, v, true);
				if(!gtPose.isNull())
				{
					groundTruth.insert(std::make_pair(iter->first, gtPose));
					if(oi==0)
					{
						idFirst = iter->first;
					}
					cloud1[oi] = pcl::PointXYZ(gtPose.x(), gtPose.y(), gtPose.z());
					cloud2[oi++] = pcl::PointXYZ(iter->second.x(), iter->second.y(), iter->second.z());
				}
			}

			// compute KITTI statistics before aligning the poses
			float t_err = 0.0f;
			float r_err = 0.0f;
			graph::calcKittiSequenceErrors(uValues(groundTruth), uValues(poses), t_err, r_err);
			printf("Ground truth comparison:\n");
			printf("   KITTI t_err = %f %%\n", t_err);
			printf("   KITTI r_err = %f deg/m\n", r_err);

			Transform t = Transform::getIdentity();
			if(oi>5)
			{
				cloud1.resize(oi);
				cloud2.resize(oi);

				t = util3d::transformFromXYZCorrespondencesSVD(cloud2, cloud1);
			}
			else if(idFirst)
			{
				t = groundTruth.at(idFirst) * poses.at(idFirst).inverse();
			}
			if(!t.isIdentity())
			{
				for(std::map<int, Transform>::iterator iter=poses.begin(); iter!=poses.end(); ++iter)
				{
					iter->second = t * iter->second;
				}
			}

			std::vector<float> translationalErrors(poses.size());
			std::vector<float> rotationalErrors(poses.size());
			float sumTranslationalErrors = 0.0f;
			float sumRotationalErrors = 0.0f;
			float sumSqrdTranslationalErrors = 0.0f;
			float sumSqrdRotationalErrors = 0.0f;
			float radToDegree = 180.0f / M_PI;
			float translational_min = 0.0f;
			float translational_max = 0.0f;
			float rotational_min = 0.0f;
			float rotational_max = 0.0f;
			oi=0;
			for(std::map<int, Transform>::iterator iter=poses.begin(); iter!=poses.end(); ++iter)
			{
				std::map<int, Transform>::const_iterator jter = groundTruth.find(iter->first);
				if(jter!=groundTruth.end())
				{
					Eigen::Vector3f vA = iter->second.toEigen3f().rotation()*Eigen::Vector3f(1,0,0);
					Eigen::Vector3f vB = jter->second.toEigen3f().rotation()*Eigen::Vector3f(1,0,0);
					double a = pcl::getAngle3D(Eigen::Vector4f(vA[0], vA[1], vA[2], 0), Eigen::Vector4f(vB[0], vB[1], vB[2], 0));
					rotationalErrors[oi] = a*radToDegree;
					translationalErrors[oi] = iter->second.getDistance(jter->second);

					sumTranslationalErrors+=translationalErrors[oi];
					sumSqrdTranslationalErrors+=translationalErrors[oi]*translationalErrors[oi];
					sumRotationalErrors+=rotationalErrors[oi];
					sumSqrdRotationalErrors+=rotationalErrors[oi]*rotationalErrors[oi];

					if(oi == 0)
					{
						translational_min = translational_max = translationalErrors[oi];
						rotational_min = rotational_max = rotationalErrors[oi];
					}
					else
					{
						if(translationalErrors[oi] < translational_min)
						{
							translational_min = translationalErrors[oi];
						}
						else if(translationalErrors[oi] > translational_max)
						{
							translational_max = translationalErrors[oi];
						}

						if(rotationalErrors[oi] < rotational_min)
						{
							rotational_min = rotationalErrors[oi];
						}
						else if(rotationalErrors[oi] > rotational_max)
						{
							rotational_max = rotationalErrors[oi];
						}
					}

					++oi;
				}
			}
			translationalErrors.resize(oi);
			rotationalErrors.resize(oi);
			if(oi)
			{
				float total = float(oi);
				float translational_rmse = std::sqrt(sumSqrdTranslationalErrors/total);
				float translational_mean = sumTranslationalErrors/total;
				float translational_median = translationalErrors[oi/2];
				float translational_std = std::sqrt(uVariance(translationalErrors, translational_mean));

				float rotational_rmse = std::sqrt(sumSqrdRotationalErrors/total);
				float rotational_mean = sumRotationalErrors/total;
				float rotational_median = rotationalErrors[oi/2];
				float rotational_std = std::sqrt(uVariance(rotationalErrors, rotational_mean));

				printf("  translational_rmse=   %f\n", translational_rmse);
				printf("  rotational_rmse=      %f\n", rotational_rmse);

				pFile = 0;
				std::string pathErrors = output+"/rtabmap_rmse"+seq+".txt";
				pFile = fopen(pathErrors.c_str(),"w");
				if(!pFile)
				{
					UERROR("could not save RMSE results to \"%s\"", pathErrors.c_str());
				}
				fprintf(pFile, "Ground truth comparison:\n");
				fprintf(pFile, "  translational_rmse=   %f\n", translational_rmse);
				fprintf(pFile, "  translational_mean=   %f\n", translational_mean);
				fprintf(pFile, "  translational_median= %f\n", translational_median);
				fprintf(pFile, "  translational_std=    %f\n", translational_std);
				fprintf(pFile, "  translational_min=    %f\n", translational_min);
				fprintf(pFile, "  translational_max=    %f\n", translational_max);
				fprintf(pFile, "  rotational_rmse=      %f\n", rotational_rmse);
				fprintf(pFile, "  rotational_mean=      %f\n", rotational_mean);
				fprintf(pFile, "  rotational_median=    %f\n", rotational_median);
				fprintf(pFile, "  rotational_std=       %f\n", rotational_std);
				fprintf(pFile, "  rotational_min=       %f\n", rotational_min);
				fprintf(pFile, "  rotational_max=       %f\n", rotational_max);
				fclose(pFile);
			}
		}
	}
	else
	{
		UERROR("Camera init failed!");
	}

	printf("Saving rtabmap database (with all statistics) to \"%s\"\n", (output+"/rtabmap" + seq + ".db").c_str());
	printf("Do:\n"
			" $ rtabmap-databaseViewer %s\n\n", (output+"/rtabmap" + seq + ".db").c_str());

	return 0;
}
