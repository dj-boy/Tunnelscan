

#include <flann/flann.h>
#include <pcl/search/pcl_search.h>
#include <pcl/common/transforms.h>
#include<Eigen/Eigenvalues>
#include <Eigen/SVD>
#include <map>
#include<math.h>
#include <iostream>
using namespace std;

namespace flann
{
	template <typename T> struct L2_Simple;
	template <typename T> class Index;
}
namespace pcl{
	template<typename PointT, typename Dist = ::flann::L2_Simple<float>>
	class Descriptor{

	public:
		Descriptor(){
			des_dim = 54;
		};
		typedef typename pcl::PointCloud<PointT> PointCloud;
		typedef typename PointCloud::Ptr PointCloudPtr;
		typedef typename PointCloud::ConstPtr PointCloudConstPtr;
		typedef typename pcl::search::Search<PointT>::Ptr SearcherPtr;
		typedef ::flann::Index<Dist> FLANNIndex;

		inline void setStddevMulThresh(double stddev_mult){
			std_mul_ = stddev_mult;
		}
		inline void setMeanK(int nr_k){
			mean_k_ = nr_k;
		}
		inline void setInputCloud(const PointCloudConstPtr &model, const PointCloudConstPtr &cloud){
			input_model = model;
			input_cloud = cloud;
			// Initialize the search class
			if (!searcher_)
			{
				if (input_model->isOrganized())
					searcher_.reset(new pcl::search::OrganizedNeighbor<PointT>());
				else
					searcher_.reset(new pcl::search::KdTree<PointT>(false));
			}
			searcher_->setInputCloud(input_model);
			if (!searcher_c)
			{
				if (input_model->isOrganized())
					searcher_c.reset(new pcl::search::OrganizedNeighbor<PointT>());
				else
					searcher_c.reset(new pcl::search::KdTree<PointT>(false));
			}
			searcher_c->setInputCloud(input_cloud);
		}
		inline void setInputKey(std::vector<int> &model, std::vector<int> &cloud){
			key_model = model;
			key_cloud = cloud;
		}
		inline void setInputNormal(pcl::PointCloud<pcl::Normal>::Ptr model, pcl::PointCloud<pcl::Normal>::Ptr cloud){
			normal_model = model;
			normal_cloud = cloud;
		}
		void getKeyPair(std::vector<pair<int, int>> &corres_point_){
			corres_point_ = corres_point;
		}
		//计算卷积矩阵
		std::vector<float> convFeatureMatrix(std::vector<Eigen::Matrix4i> &FeatureMatrix);
		//特征向量加法
		void addvector(std::vector<float> &sum, std::vector<float> &vc);
		//特征向量除法
		void dividevector(std::vector<float> &sum, int num);
		//计算合成特征描述子
		void computeDescriptor(std::vector<int> &key_v, SearcherPtr search, PointCloudConstPtr cloud, std::vector<std::vector<float>> &des_v);
		//建立模型中合成特征描述子的kdtree
		void buildModelIndex();
		//匹配关键点
		void matchKeyPoint();
		//根据对应点旋转input_cloud
		void applytransform(PointCloudPtr output, int count, bool ComputeError);
		//迭代最近点
		void  applyICPtransform(PointCloudPtr output, int count, bool ComputeError);

	private:
		int des_dim;//算子的维度;
		PointCloudConstPtr input_model, input_cloud;
		SearcherPtr searcher_, searcher_c;
		pcl::PointCloud<pcl::Normal>::Ptr normal_model, normal_cloud;
		std::vector<int> key_model, key_cloud;
		int mean_k_, std_mul_;
		std::vector<std::vector<float>> des, des_cloud;//存储key_point的合成特征描述子
		boost::shared_ptr<FLANNIndex> flann_index;
		boost::shared_array<float> des_cloud_;
		std::vector<pair<int, int>> corres_point;//用于存储对应点对
	};

	

	template <typename PointT, typename Dist> std::vector<float>
		pcl::Descriptor<PointT, Dist>::convFeatureMatrix(std::vector<Eigen::Matrix4i> &FeatureMatrix){
			int t[8][3] = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 0, 0 }, { 1, 0, 1 }, { 1, 1, 0 }, { 1, 1, 1 } };
			float sum, allsum = 0;
			std::vector<float> res(27);
			for (int i = 0; i < 3; i++){
				for (int j = 0; j < 3; j++){
					for (int k = 0; k < 3; k++){
						sum = 0;
						for (int l = 0; l < 8; l++){
							sum += FeatureMatrix[i + t[l][0]](j + t[l][1], k + t[l][2]);
						}
						res[9 * i + 3 * j + k] = sum;
						allsum += sum;
					}
				}
			}
			allsum /= 27;
			for (int i = 0; i < 27; i++){
				res[i] = res[i] / allsum;
			}
			return res;
		}

		template <typename PointT, typename Dist> void
			pcl::Descriptor<PointT, Dist>::addvector(std::vector<float> &sum, std::vector<float> &vc){
				int size = sum.size();
				for (int i = 0; i < size; i++){
					sum[i] += vc[i];
				}
			}

		template <typename PointT, typename Dist> void
			pcl::Descriptor<PointT, Dist>::dividevector(std::vector<float> &sum, int num){
				int size = sum.size();
				for (int i = 0; i < size; i++){
					sum[i] /= num;
				}
			}

		template <typename PointT, typename Dist> void
			pcl::Descriptor<PointT, Dist>::computeDescriptor(std::vector<int> &key_v, SearcherPtr search, PointCloudConstPtr cloud, std::vector<std::vector<float>> &des_v){
				map<int, std::vector<float>> mp_feature;//存储key_point的初始特征描述子
				if (key_v.size() == 0)return;
				// The arrays to be used for knn
				std::vector<int> n_indices(mean_k_);
				std::vector<float> n_dists(mean_k_);
				int find_, tmp_;
				for (int l = 0; l < key_v.size(); l++){//计算每个key_point的合成特征描述子
					tmp_ = key_v[l];
					find_ = search->radiusSearch(tmp_, std_mul_, n_indices, n_dists, mean_k_);//合成特征描述子的计算域
					int tmp, findnum;
					// The arrays to be used for knn
					std::vector<int> nn_indices(mean_k_);
					std::vector<float> nn_dists(mean_k_);
					std::vector<float> ori_sum_descriptor(27, 0), ori_kp_descriptor, fea_vf;
					for (int i = 0; i < find_; i++){//计算每个key_point的初始特征描述子
						tmp = n_indices[i];
						map<int, std::vector<float>>::iterator it = mp_feature.find(tmp);
						if (it != mp_feature.end()){//计算过直接跳过
							if (n_dists[i] < 0.0001)
								ori_kp_descriptor = it->second;
							addvector(ori_sum_descriptor, it->second);
							continue;
						}
						findnum = search->radiusSearch(tmp, std_mul_, nn_indices, nn_dists, mean_k_);//初始特征描述子的计算域
						std::vector<Eigen::Matrix4i> hist(4, Eigen::Matrix4i::Zero());//初始投影矩阵（4*4*4）
						for (int j = 0; j < findnum; j++){//每个邻近点计算各自的初始特征描述子（27维）
							if (nn_dists[j] < 0.001)continue;//自身
							float dz = cloud->points[nn_indices[j]].z - cloud->points[tmp].z,
								dx = cloud->points[nn_indices[j]].x - cloud->points[tmp].x,
								dy = cloud->points[nn_indices[j]].y - cloud->points[tmp].y,
								angle = 0;
							//纵坐标不随旋转而改变
							int line = 0, col = 0;
							if (dz>0 && dz < 30)
								line = 1;
							else if (dz<0 && dz>-30)
								line = 2;
							else if (dz < 0 && dz < -30)
								line = 3;
							//横坐标随着旋转改变
							for (int k = 0; k < 4; k++){
								float ax = cos(angle), ay = sin(angle), inpro = ax*dx + ay*dy;
								if (inpro>0 && inpro < 30)
									col = 1;
								else if (inpro<0 && inpro>-30)
									col = 2;
								else if (inpro < 0 && inpro < -30)
									col = 3;
								hist[k](line, col)++;
								angle += 45 * 3.1415926 / 180;
							}

						}
						fea_vf = convFeatureMatrix(hist);
						mp_feature[tmp] = fea_vf;
						if (n_dists[i] < 0.0001)
							ori_kp_descriptor = fea_vf;
						addvector(ori_sum_descriptor, fea_vf);
					}
					//合成算子
					dividevector(ori_sum_descriptor, find_);
					for (int i = 0; i < 27; i++){
						ori_kp_descriptor.push_back(ori_sum_descriptor[i]);
					}
					des_v.push_back(ori_kp_descriptor);
				}
			}

		template <typename PointT, typename Dist> void
			pcl::Descriptor<PointT, Dist>::buildModelIndex(){
				computeDescriptor(key_model, searcher_, input_model, des);//计算model中关键点的合成特征描述子
				int rows = key_model.size();
				boost::shared_array<float> des_;
				des_.reset(new float[rows*des_dim]);
				for (int i = 0; i < rows; i++){
					for (int j = 0; j < des_dim; j++){
						*(des_.get() + i*des_dim + j) = des[i][j];
					}
				}
				//des.clear();//清空des，释放内存
				flann_index.reset(new FLANNIndex(::flann::Matrix<float>(des_.get(), rows, des_dim), ::flann::KDTreeSingleIndexParams(15)));
				flann_index->buildIndex();
			}

		template <typename PointT, typename Dist> void
			pcl::Descriptor<PointT, Dist>::matchKeyPoint(){
				computeDescriptor(key_cloud, searcher_c, input_cloud, des_cloud);//计算cloud中关键点的合成特征描述子
				int size = key_cloud.size();
				boost::shared_array<float> des_cloud_;
				des_cloud_.reset(new float[size*des_dim]);
				for (int i = 0; i < size; i++){
					for (int j = 0; j < des_dim; j++){
						*(des_cloud_.get() + i*des_dim + j) = des_cloud[i][j];
					}
				}
				int k = 10;
				std::vector<int> nn_indices(size * k);
				std::vector<float> nn_dist(size * k);
				flann::Matrix<int> indices_mat(&nn_indices[0], size, k);
				flann::Matrix<float> dist_mat(&nn_dist[0], size, k);
				flann_index->knnSearch(flann::Matrix<float>(des_cloud_.get(), size, des_dim), indices_mat, dist_mat, k, flann::SearchParams(-1, 0.0f));
				map<int, pair<int, double>> mp_cor_p;
				for (int i = 0; i < size*k; i++){
					float dis = pow(input_model->points[key_model[nn_indices[i]]].x - input_cloud->points[key_cloud[i/k]].x, 2) +
						pow(input_model->points[key_model[nn_indices[i]]].y - input_cloud->points[key_cloud[i/k]].y, 2) +
						pow(input_model->points[key_model[nn_indices[i]]].z - input_cloud->points[key_cloud[i/k]].z, 2);
					if (nn_dist[i] < 4 && dis<90000){
						//if (sqrt(dis)*0.7>abs(input_model->points[key_model[nn_indices[i]]].x - input_cloud->points[key_cloud[i / k]].x))continue;
						//if (abs(normal_model->points[key_model[nn_indices[i]]].normal_x*normal_cloud->points[key_cloud[i / k]].normal_x +
						//	normal_model->points[key_model[nn_indices[i]]].normal_y*normal_cloud->points[key_cloud[i / k]].normal_y +
						//	normal_model->points[key_model[nn_indices[i]]].normal_x*normal_cloud->points[key_cloud[i / k]].normal_x) < 0.8)continue;
						if (mp_cor_p.find(key_model[nn_indices[i]]) != mp_cor_p.end()){
							if (mp_cor_p[key_model[nn_indices[i]]].second < nn_dist[i])
								mp_cor_p[key_model[nn_indices[i]]] = make_pair(key_cloud[i / k], nn_dist[i]);
						}
						else
							mp_cor_p[key_model[nn_indices[i]]] = make_pair(key_cloud[i / k], nn_dist[i]);
					}
				}
				for (map<int, pair<int, double>>::iterator it = mp_cor_p.begin(); it != mp_cor_p.end(); it++){
					corres_point.push_back(make_pair(it->first, it->second.first));
				}
				cout <<"匹配点对: "<< corres_point.size() << endl;
			}

		template <typename PointT,typename Dist> void
			pcl::Descriptor<PointT, Dist>::applytransform(PointCloudPtr output, int count, bool ComputeError){
				if (output->points.size() != 0)input_cloud = output;
				int size = corres_point.size();
				std::vector<int> v1, v2;
				std::vector<pair<int, int>> corres_point_;
				for (int i = 0; i < size; i++){
					float dis = pow(input_model->points[corres_point[i].first].x - input_cloud->points[corres_point[i].second].x, 2) +
						pow(input_model->points[corres_point[i].first].y - input_cloud->points[corres_point[i].second].y, 2) +
						pow(input_model->points[corres_point[i].first].z - input_cloud->points[corres_point[i].second].z, 2);
					if (dis>90000 / count)continue;
					v1.push_back(corres_point[i].first);
					v2.push_back(corres_point[i].second);
					corres_point_.push_back(corres_point[i]);
				}
				corres_point = corres_point_;
				cout <<"剩余对应点对:" <<v1.size() << endl;
				if (v1.size() < 5){
					cout << "对应点过少" << endl;
					return;
				}

				Eigen::Vector4f centroid_src, centroid_tra;
				Eigen::Vector3f p, p_dot, t;
				Eigen::Matrix4f transform_m = Eigen::Matrix4f::Identity();
				Eigen::Matrix3f H = Eigen::Matrix3f::Zero(), R;

				pcl::compute3DCentroid(*input_model, v1, centroid_src);
				pcl::compute3DCentroid(*input_cloud, v2, centroid_tra);

				p(0) = centroid_src(0), p(1) = centroid_src(1), p(2) = centroid_src(2);
				p_dot(0) = centroid_tra(0), p_dot(1) = centroid_tra(1), p_dot(2) = centroid_tra(2);

				H = Eigen::Matrix3f::Zero();
				for (int i = 0; i < v1.size(); i++){
					H(0, 0) += (input_cloud->points[v2[i]].x - p_dot(0))*(input_model->points[v1[i]].x - p(0));
					H(0, 1) += (input_cloud->points[v2[i]].x - p_dot(0))*(input_model->points[v1[i]].y - p(1));
					H(0, 2) += (input_cloud->points[v2[i]].x - p_dot(0))*(input_model->points[v1[i]].z - p(2));
					H(1, 0) += (input_cloud->points[v2[i]].y - p_dot(1))*(input_model->points[v1[i]].x - p(0));
					H(1, 1) += (input_cloud->points[v2[i]].y - p_dot(1))*(input_model->points[v1[i]].y - p(1));
					H(1, 2) += (input_cloud->points[v2[i]].y - p_dot(1))*(input_model->points[v1[i]].z - p(2));
					H(2, 0) += (input_cloud->points[v2[i]].z - p_dot(2))*(input_model->points[v1[i]].x - p(0));
					H(2, 1) += (input_cloud->points[v2[i]].z - p_dot(2))*(input_model->points[v1[i]].y - p(1));
					H(2, 2) += (input_cloud->points[v2[i]].z - p_dot(2))*(input_model->points[v1[i]].z - p(2));
				}
				Eigen::JacobiSVD<Eigen::MatrixXf> svd(H, Eigen::ComputeThinU | Eigen::ComputeThinV);
				Eigen::Matrix3f V = svd.matrixV(), U = svd.matrixU();
				R = V*U.transpose();
				t = p - R*p_dot;
				Eigen::Matrix4f res = Eigen::Matrix4f::Identity();
				res(0, 0) = R(0, 0), res(0, 1) = R(0, 1), res(0, 2) = R(0, 2), res(0, 3) = t(0);
				res(1, 0) = R(1, 0), res(1, 1) = R(1, 1), res(1, 2) = R(1, 2), res(1, 3) = t(1);
				res(2, 0) = R(2, 0), res(2, 1) = R(2, 1), res(2, 2) = R(2, 2), res(2, 3) = t(2);
				cout << res << endl;
				//添加代码，实现key_point的旋转与平移
				pcl::transformPointCloud<pcl::PointXYZ>(*input_cloud, *output, res);
				if (ComputeError){
					float sum_dis = 0;
					for (int i = 0; i < v1.size(); i++){
						sum_dis += pow(input_model->points[corres_point[i].first].x - output->points[corres_point[i].second].x, 2) +
							pow(input_model->points[corres_point[i].first].y - output->points[corres_point[i].second].y, 2) +
							pow(input_model->points[corres_point[i].first].z - output->points[corres_point[i].second].z, 2);
					}
					cout << "平均误差" << sqrt(sum_dis / v1.size()) << endl;
				}
			}

		template <typename PointT,typename Dist> void
			pcl::Descriptor<PointT, Dist>::applyICPtransform(PointCloudPtr output, int count, bool ComputeError){
				int size = output->points.size();
				
			}
}
