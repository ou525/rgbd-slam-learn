/*
	file name: 		slam.cpp
	author: 		sheng ou
	mail:			ousheng525@gmail.com
	created time:	2017/03/31
*/

#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;

#include "slamBase.h"

#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/passthrough.h>

#include <g2o/types/slam3d/types_slam3d.h>
#include <g2o/core/sparse_optimizer.h>
#include <g2o/core/block_solver.h>
#include <g2o/core/factory.h>
#include <g2o/core/optimization_algorithm_factory.h>
#include <g2o/core/optimization_algorithm_gauss_newton.h>
#include <g2o/solvers/csparse/linear_solver_csparse.h>
#include <g2o/core/robust_kernel.h>
#include <g2o/core/robust_kernel_factory.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/solvers/eigen/linear_solver_eigen.h>

typedef g2o::BlockSolver_6_3 SlamBlockSolver;
typedef g2o::LinearSolverEigen<SlamBlockSolver::PoseMatrixType> SlamLinearSolver;

// 给定index，读取一帧数据
FRAME readFrame(int index, ParameterReader& pd);

//度量运动的大小
double normofTransform(cv::Mat rvec, cv::Mat tvec);

 // 检测两个帧，结果定义
 enum CHECK_RESULT
 {
 	NOT_MATCHED = 0,
 	TOO_FAR_AWAY,
 	TOO_CLOSE,
 	KEYFRAME
 };

// 函数声明
 CHECK_RESULT checkKeyframes(FRAME& f1, FRAME& f2, g2o::SparseOptimizer& opti, bool is_loops=false);

// 检测近距离的回环
void checkNearbyLoops(std::vector<FRAME>& frames, FRAME& currFrame, g2o::SparseOptimizer& opti);

// 随机检测回环
void checkRandomLoops(vector<FRAME>& frames, FRAME& currFrame, g2o::SparseOptimizer& opti);




int main( int argc, char** argv)
{
	ParameterReader pd;
	int startIndex = atoi(pd.getData("start_index").c_str());
	int endIndex   = atoi(pd.getData("end_index"  ).c_str());

	// 所有的关键帧都放在了这里
	vector<FRAME> keyframes;

	//initialize
	cout << "initializing..." << endl;
	int currIndex = startIndex; // 当前索引为currIndex
	FRAME currFrame = readFrame(currIndex, pd); // 上一帧数据

	
	string detector = pd.getData("detector");
	CAMERA_INTRINSIC_PARAMETERS camera = getDefaultCamera();
	computeKeyPointsAndDesp(currFrame, detector);
	PointCloud::Ptr cloud = image2PointCloud(currFrame.rgb, currFrame.depth, camera);

	
	//pcl::visualization::CloudViewer viewer("viewer");

	// // 是否显示点云
	// bool visualize = pd.getData("visualize_pointcloud")==string("yes");

	// int min_inliers = atoi(pd.getData("min_inliers").c_str());
	// double max_norm    = atof(pd.getData("max_norm"   ).c_str());
	// //cout << min_inliers << "   " << max_norm << endl;

	/*
	//新增:有关g2o的初始化
	*/
	// 选择优化方法
	
	//初始化求解器
	SlamLinearSolver* linearSolver = new SlamLinearSolver();
	linearSolver->setBlockOrdering(false);
	SlamBlockSolver* blockSolver = new SlamBlockSolver(linearSolver);
	g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(blockSolver);
	
	
	g2o::SparseOptimizer globalOptimizer; // 最后用的就是这个东东
	globalOptimizer.setAlgorithm(solver);
	// 不要输出调试信息
	globalOptimizer.setVerbose(false);

	// 向globalOptimizer增加第一个顶点
	g2o::VertexSE3* v = new g2o::VertexSE3();
	v->setId(currIndex);
	v->setEstimate(Eigen::Isometry3d::Identity());//估计为单位矩阵
	v->setFixed(true); //第一个顶点固定，不用优化
	globalOptimizer.addVertex(v);

	//int lastIndex = currIndex; //上一帧的id
	keyframes.push_back(currFrame);

	double keyframe_threshold = atof(pd.getData("keyframe_threshold").c_str());

	bool check_loop_closure = pd.getData("check_loop_closure") == string("yes");


	for(currIndex=startIndex+1;currIndex<endIndex;currIndex++)
	{
		cout << "Reading files " << currIndex << endl;

		FRAME currFrame = readFrame(currIndex, pd);	// 读取currFrame
		computeKeyPointsAndDesp(currFrame, detector);

		// // 比较currFrame 和 lastFrame
		// RESULT_OF_PNP result = estimateMotion(lastFrame, currFrame, camera);
		// if(result.inliers < min_inliers) //inliers不够，放弃该帧
		// 	continue;
		// // 计算运动范围是否太大
		// double norm = normofTransform(result.rvec, result.tvec);
		// cout << "norm = " << norm << endl;
		// if(norm >= max_norm)
		// 	continue;

		//匹配该帧与keyframes里最后一帧
		CHECK_RESULT result = checkKeyframes(keyframes.back(), currFrame, globalOptimizer);
		switch(result) //根据匹配结果不同采取不同策略
		{
			case NOT_MATCHED:
				//没匹配上，直接跳过
				cout << RED"not enough inliers. " << endl;
				break;
			case TOO_FAR_AWAY:
				// 太近了，也直接跳
				cout << RED"too far away, may be an error. " << endl;
				break;
			case TOO_CLOSE:
				// 太远了，可能出错了
				cout << RESET"too close, not a keyframe" << endl;
				break;
			case KEYFRAME:
				cout << GREEN"this is a new keyframe" << endl;
	            // 不远不近，刚好
	            /**
	             * This is important!!
	             * This is important!!
	             * This is important!!
	             * (very important so I've said three times!)
	             */
				// 检测回环
				if(check_loop_closure)
				{
					checkNearbyLoops(keyframes, currFrame, globalOptimizer);
					checkRandomLoops(keyframes, currFrame, globalOptimizer);
				}
            	keyframes.push_back(currFrame);
                 	break;
        	default:
        		break; 	

		}


		// Eigen::Isometry3d T = cvMat2Eigen(result.rvec, result.tvec);
		// cout << "T=" << T.matrix() << endl;

		// // 去掉可视化的话，会快一些
		// if(visualize == true)
		// {
		// 	cloud = joinPointCloud(cloud, currFrame, T, camera);
		// 	viewer.showCloud(cloud);
		// }

		// //cloud = joinPointCloud(cloud, currFrame, T, camera);

		// // 向g2o中增加这个顶点与上一帧联系的边
		// // 顶点部分
		// // 顶点只需设定id即可
		// g2o::VertexSE3* v = new g2o::VertexSE3();
		// v->setId(currIndex);
		// v->setEstimate(Eigen::Isometry3d::Identity());
		// globalOptimizer.addVertex(v);

		// // 边部分
		// g2o::EdgeSE3* edge = new g2o::EdgeSE3();
		// // 连接此边的两个顶点id
		// edge->vertices()[0] = globalOptimizer.vertex(lastIndex);
		// edge->vertices()[1] = globalOptimizer.vertex(currIndex);
		// // 信息矩阵
		// Eigen::Matrix<double,6,6> information = Eigen::Matrix<double,6,6>::Identity();
		// // 信息矩阵是协方差矩阵的逆，表示我们对边的精度的预先估计
		// // 因为pose为6D的，信息矩阵是6*6的阵，假设位置和角度的估计精度均为0.1且互相独立
		// // 那么协方差则为对角为0.01的矩阵，信息阵则为100的矩阵
		// information(0,0) = information(1,1) = information(2,2) = 100;
		// information(3,3) = information(4,4) = information(5,5) = 100;
		// // 也可以将角度设大一些，表示对角度的估计更加准确
		// edge->setInformation(information);
		// // 边的估计即是pnp求解之结果
		// edge->setMeasurement(T);
		// // 将此边加入图中
		// globalOptimizer.addEdge(edge);	

		// // if(visualize == true)
		// // 	viewer.showCloud(cloud);

		// lastFrame = currFrame;
		// lastIndex = currIndex;
	}

	//pcl::io::savePCDFile("data/result.pcd", *cloud);

	// 优化所有边
	cout << RESET"optimizing pose graph, vertices: " << globalOptimizer.vertices().size() << endl;
	globalOptimizer.save("./data/result_before.g2o");
	globalOptimizer.initializeOptimization();
	globalOptimizer.optimize(100);//可以指定优化步数
	globalOptimizer.save("./data/result_after.g2o");
	cout << "Optimization done. " << endl;

	// 拼接点云地图
	cout << "saving the point cloud map ..." << endl;
	PointCloud::Ptr output (new PointCloud());//全局地图
	PointCloud::Ptr tmp (new PointCloud());

	pcl::VoxelGrid<PointT> voxel; // 网格滤波器，调整地图分辨率
	pcl::PassThrough<PointT> pass; // z方向区间滤波器，由于rgbd相机的有效深度区间有限，把太远的去掉
	pass.setFilterFieldName("z");
	pass.setFilterLimits(0.0, 10.0); ////4m以上就不要了
	pass.setFilterLimitsNegative(false);

	double gridsize = atof(pd.getData("voxel_grid").c_str()); //分辨图可以在parameters.txt里调
	voxel.setLeafSize(gridsize, gridsize, gridsize);

	for(size_t i=0;i<keyframes.size();i++)
	{
		// 从g2o里取出一帧
		g2o::VertexSE3* vertex = dynamic_cast<g2o::VertexSE3*>(globalOptimizer.vertex(keyframes[i].frameID));
		Eigen::Isometry3d pose = vertex->estimate(); //该帧优化后的位姿
		PointCloud::Ptr newCloud = image2PointCloud(keyframes[i].rgb, keyframes[i].depth, camera); //转成点云
		
		//以下是滤波
		voxel.setInputCloud(newCloud);
		voxel.filter(*tmp);
		//cout<<"55555555555555555555555 point cloud size = "<<newCloud->points.size()<<endl;
		//cout<<"44444444444444444444444 point cloud size = "<<tmp->points.size()<<endl;
		// pass.setInputCloud(tmp);
		// pass.filter(*newCloud);
		// cout<<"11111111111111111111111 point cloud size = "<<newCloud->points.size()<<endl;
		//把点云变换后加入全局地图中
		pcl::transformPointCloud(*tmp, *newCloud, pose.matrix());
		//cout<<"33333333333333333333333 point cloud size = "<<tmp->points.size()<<endl;
		*output += *newCloud;
		//cout<<"22222222222222222222222 point cloud size = "<<output->points.size()<<endl;
		tmp->clear();
		newCloud->clear();			
	}

	voxel.setInputCloud(output);
	voxel.filter(*tmp);

	cout<<"point cloud size = "<<tmp->points.size()<<endl;
	//存储
	pcl::io::savePCDFile( "./result.pcd", *tmp);

	cout << "Final map is saved. " << endl;
	//globalOptimizer.clear();

	return 0;
}

FRAME readFrame(int index, ParameterReader& pd)
{
	FRAME f;
	string rgbDir = pd.getData("rgb_dir");
	string depthDir = pd.getData("depth_dir");

	string rgbExt = pd.getData("rgb_extension");
	string depthExt = pd.getData("depth_extension");

	stringstream ss;
	ss << rgbDir << index << rgbExt;
	string filename;
	ss >> filename;
	f.rgb = cv::imread(filename);

	ss.clear();
	filename.clear();
	ss <<depthDir<<index<<depthExt;
	ss >> filename;

	f.depth = cv::imread(filename, -1);
	f.frameID = index;
	return f;
}

double normofTransform(cv::Mat rvec, cv::Mat tvec)
{
	return fabs(min(cv::norm(rvec), 2*M_PI-cv::norm(rvec))) + fabs(cv::norm(tvec));
}

CHECK_RESULT checkKeyframes(FRAME& f1, FRAME& f2, g2o::SparseOptimizer& opti, bool is_loops)
{
	static ParameterReader pd;
	static int min_inliers = atoi(pd.getData("min_inliers").c_str());
	static double max_norm = atof(pd.getData("max_norm").c_str());
	static double keyframe_threshold = atof(pd.getData("keyframe_threshold").c_str());

	static double max_norm_lp = atof(pd.getData("max_norm_lp").c_str());
	static CAMERA_INTRINSIC_PARAMETERS camera = getDefaultCamera();
	static g2o::RobustKernel* robust_kernel = g2o::RobustKernelFactory::instance()->construct("Cauchy");
	// 比较f1 和 f2
	RESULT_OF_PNP result = estimateMotion(f1, f2, camera);
	if(result.inliers < min_inliers) //inliers不够，放弃该帧
		return NOT_MATCHED;
	// 计算运动范围是否太大
	double norm = normofTransform(result.rvec, result.tvec);
	if( is_loops == false )
	{
		if( norm >= max_norm )
			return TOO_FAR_AWAY;
	}
	else
	{
		if( norm >= max_norm_lp )
			return TOO_FAR_AWAY;
	}

	if( norm <= keyframe_threshold )
		return TOO_CLOSE;

	// 向g2o中增加这个顶点与上一帧联系的边
	// 顶点部分
	// 顶点只需设定id即可
	if( is_loops == false )
	{
		g2o::VertexSE3* v = new g2o::VertexSE3();
		v->setId(f2.frameID);
		v->setEstimate(Eigen::Isometry3d::Identity());
		opti.addVertex(v);
	}

	//边部分
	g2o::EdgeSE3* edge = new g2o::EdgeSE3();
	// 连接此边的两个顶点id
	// edge->vertices()[0] = opti.vertex(f1.frameID);
	// edge->vertices()[1] = opti.vertex(f2.frameID);
	// edge->setRobustKernel(robust_kernel);
	edge->setVertex(0, opti.vertex(f1.frameID));
	edge->setVertex(1, opti.vertex(f2.frameID));
	// edge->setRobustKernel( new g2o::RobustKernelHuber() );
	edge->setRobustKernel(robust_kernel);

	// 信息矩阵
	Eigen::Matrix<double, 6, 6> information = Eigen::Matrix<double, 6, 6>::Identity();
	// 信息矩阵是协方差矩阵的逆，表示我们对边的精度的预先估计
	// 因为pose为6D的，信息矩阵是6*6的阵，假设位置和角度的估计精度均为0.1且互相独立
	// 那么协方差则为对角为0.01的矩阵，信息阵则为100的矩阵
	information(0, 0) = information(1, 1) = information(2, 2) = 100;
	information(3, 3) = information(4, 4) = information(5, 5) = 100;
	// 也可以将角度设大一些，表示对角度的估计更加准确
	edge->setInformation(information);
	// 边的估计即是pnp求解之结果
	Eigen::Isometry3d T = cvMat2Eigen(result.rvec, result.tvec);
	edge->setMeasurement(T.inverse());
	// 将此边加入图中
	opti.addEdge(edge);
	return KEYFRAME;
}

void checkNearbyLoops(vector<FRAME>& frames, FRAME& currFrame, g2o::SparseOptimizer& opti)
{
	static ParameterReader pd;
	static int nearby_loops = atoi(pd.getData("nearby_loops").c_str());

	// 就是把currFrame和 frames里末尾几个测一遍
	if( frames.size() <= nearby_loops )
	{
		// no enough keyframes, check everyone
		for(size_t i=0;i<frames.size();i++)
		{
			checkKeyframes(frames[i], currFrame, opti, true);
		}
	}
	else
	{
		//check the nearest ones
		for(size_t i=frames.size()-nearby_loops;i<frames.size();i++)
		{
			checkKeyframes(frames[i], currFrame, opti, true);
		}
	}
}


void checkRandomLoops(vector<FRAME>& frames, FRAME& currFrame, g2o::SparseOptimizer& opti)
{
	static ParameterReader pd;
	static int random_loops = atoi(pd.getData("random_loops").c_str());
	srand((unsigned int) time(NULL));
	// 随机取一些帧进行检测

	if(frames.size() <= random_loops)
	{
		// no enough keyframe, check everyone
		for(size_t i=0;i<frames.size();i++)
		{
			checkKeyframes(frames[i], currFrame, opti, true);
		}
	}
	else
	{
		//randomly check loops
		for(int i=0;i<random_loops;i++)
		{
			int index = rand() % frames.size();
			checkKeyframes(frames[index], currFrame, opti, true);
		}
	}
}