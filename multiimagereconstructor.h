#ifndef MULTILINEARRECONSTRUCTION_MULTIIMAGERECONSTRUCTOR_H
#define MULTILINEARRECONSTRUCTION_MULTIIMAGERECONSTRUCTOR_H

#ifndef MKL_BLAS
#define MKL_BLAS MKL_DOMAIN_BLAS
#endif

#define EIGEN_USE_MKL_ALL

#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Geometry>
#include <eigen3/Eigen/LU>

#include "ceres/ceres.h"

#include "basicmesh.h"
#include "common.h"
#include "constraints.h"
#include "costfunctions.h"
#include "multilinearmodel.h"
#include "parameters.h"
#include "singleimagereconstructor.hpp"
#include "statsutils.h"
#include "utils.hpp"

#include "OffscreenMeshVisualizer.h"

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

namespace fs = boost::filesystem;

using namespace Eigen;

template <typename Constraint>
class MultiImageReconstructor {
public:
  MultiImageReconstructor() {}

  void LoadModel(const string& filename) {
    model = MultilinearModel(filename);
    single_recon.LoadModel(filename);
  }
  void LoadPriors(const string& filename_id, const string& filename_exp) {
    prior.load(filename_id, filename_exp);
    single_recon.LoadPriors(filename_id, filename_exp);
  }
  void SetContourIndices(const vector<vector<int>>& contour_indices_in) {
    contour_indices = contour_indices_in;
    single_recon.SetContourIndices(contour_indices_in);
  }
  void SetMesh(const BasicMesh& mesh) {
    template_mesh = mesh;
  }
  void SetIndices(const vector<int>& indices) {
    init_indices = indices;
  }

  void AddImagePointsPair(const string& filename, const pair<QImage, vector<Constraint>>& p) {
    image_filenames.push_back(filename);
    image_points_pairs.push_back(p);
  }

  bool Reconstruct();

  const Vector3d& GetRotation(int imgidx) const { return param_sets[imgidx].model.R; }
  const Vector3d& GetTranslation(int imgidx) const { return param_sets[imgidx].model.T; }
  const VectorXd& GetIdentityWeights(int imgidx) const { return param_sets[imgidx].model.Wid; }
  const VectorXd& GetExpressionWeights(int imgidx) const { return param_sets[imgidx].model.Wexp_FACS; }
  const Tensor1& GetGeometry(int imgidx) const {
    model.ApplyWeights(GetIdentityWeights(imgidx), GetExpressionWeights(imgidx));
    return model.GetTM();
  }
  const CameraParameters GetCameraParameters(int imgidx) const { return param_sets[imgidx].cam; }
  const vector<int> GetIndices(int imgidx) const { return param_sets[imgidx].indices; }
  vector<int> GetUpdatedIndices(int imgidx) const {
    vector<int> idxs;
    for(int i=0;i<param_sets[imgidx].recon.cons.size();++i) {
      idxs.push_back(param_sets[imgidx].recon.cons[i].vidx);
    }
    return idxs;
  }

protected:
  void VisualizeReconstructionResult(const fs::path& folder, int i) {
    // Visualize the reconstruction results
    #if 0
    MeshVisualizer w("reconstruction result", param_sets[i].mesh);
    w.BindConstraints(image_points_pairs[i].second);
    w.BindImage(image_points_pairs[i].first);
    w.BindLandmarks(init_indices);

    w.BindUpdatedLandmarks(param_sets[i].indices);
    w.SetMeshRotationTranslation(param_sets[i].model.R, param_sets[i].model.T);
    w.SetCameraParameters(param_sets[i].cam);
    w.resize(image_points_pairs[i].first.width(), image_points_pairs[i].first.height());
    w.show();
    w.paintGL();
    w.update();

    QImage recon_image = w.grabFrameBuffer();
    fs::path image_path = fs::path(image_filenames[i]);

    recon_image.save( (folder / fs::path(image_path.stem().string() + ".png")).string().c_str() );
    #else
    OffscreenMeshVisualizer visualizer(image_points_pairs[i].first.width(),
                                       image_points_pairs[i].first.height());

    visualizer.SetMVPMode(OffscreenMeshVisualizer::CamPerspective);
    visualizer.SetRenderMode(OffscreenMeshVisualizer::Mesh);
    visualizer.BindMesh(param_sets[i].mesh);
    visualizer.SetCameraParameters(param_sets[i].cam);
    visualizer.SetMeshRotationTranslation(param_sets[i].model.R, param_sets[i].model.T);
    visualizer.SetIndexEncoded(false);
    visualizer.SetEnableLighting(true);

    QImage img = visualizer.Render(true);
    fs::path image_path = fs::path(image_filenames[i]);
    img.save((folder / fs::path(image_path.stem().string() + ".png")).string().c_str());
    #endif
  }

private:
  MultilinearModel model;
  MultilinearModelPrior prior;
  vector<vector<int>> contour_indices;
  vector<int> init_indices;
  BasicMesh template_mesh;

  struct ParameterSet {
    vector<int> indices;
    BasicMesh mesh;

    CameraParameters cam;
    ModelParameters model;
    ReconstructionParameters<Constraint> recon;
    OptimizationParameters opt;
    ReconstructionStats stats;
  };

  // Input image points pairs
  vector<pair<QImage, vector<Constraint>>> image_points_pairs;
  vector<string> image_filenames;

  // A set of parameters for each image
  vector<ParameterSet> param_sets;

  // The worker for single image reconstruction
  SingleImageReconstructor<Constraint> single_recon;
};

namespace {
  void safe_create(const fs::path& p) {
    if(!fs::exists(p)) {
      fs::create_directory(p);
    }
  }
}

template <typename Constraint>
bool MultiImageReconstructor<Constraint>::Reconstruct() {
  cout << "Reconstruction begins..." << endl;

  // Misc stuff
  cout << image_filenames.size() << endl;
  fs::path image_path = fs::path(image_filenames.front()).parent_path();
  fs::path result_path = image_path / fs::path("multi_recon");
  cout << "creating directory " << result_path.string() << endl;
  safe_create(result_path);
  cout << "directory created ..." << endl;

  // Initialize the parameter sets
  param_sets.resize(image_points_pairs.size());
  for(size_t i=0;i<param_sets.size();++i) {
    auto& params = param_sets[i];
    params.indices = init_indices;
    params.mesh = template_mesh;

    const int image_width = image_points_pairs[i].first.width();
    const int image_height = image_points_pairs[i].first.height();

    // camera parameters
    cout << image_width << "x" << image_height << endl;
    params.cam = CameraParameters::DefaultParameters(image_width, image_height);
    cout << params.cam.image_size.x << ", " << params.cam.image_size.y << endl;

    // model parameters
    params.model = ModelParameters::DefaultParameters(prior.Uid, prior.Uexp);

    // reconstruction parameters
    params.recon.cons = image_points_pairs[i].second;
    params.recon.imageWidth = image_width;
    params.recon.imageHeight = image_height;
  }

  const int num_images = image_points_pairs.size();

  VectorXd identity_centroid;

  // Main reconstruction loop
  //  1. Use single image reconstructor to do per-image reconstruction first
  //  2. Select a consistent set of images for joint reconstruction
  //  3. Convergence test. If not converged, goto step 1.
  const int max_iters_main_loop = 3;
  int iters_main_loop = 0;

  vector<MatrixXd> identity_weights_history;
  vector<VectorXd> identity_weights_centroid_history;

  vector<int> consistent_set(num_images);
  for(int i=0;i<num_images;++i) consistent_set[i] = i;

  while(iters_main_loop++ < 3){
    fs::path step_result_path = result_path / fs::path("step" + to_string(iters_main_loop));
    safe_create(step_result_path);

    // Single image reconstruction step
    OptimizationParameters opt_params = OptimizationParameters::Defaults();
    opt_params.w_prior_id = 10 * iters_main_loop;
    opt_params.w_prior_exp = 10;

    fs::path step_single_recon_result_path = step_result_path / fs::path("single_recon");
    safe_create(step_single_recon_result_path);
    for(int i=0;i<num_images;++i) {
      single_recon.SetMesh(param_sets[i].mesh);
      single_recon.SetIndices(param_sets[i].indices);
      single_recon.SetImageSize(param_sets[i].recon.imageWidth, param_sets[i].recon.imageHeight);
      single_recon.SetConstraints(param_sets[i].recon.cons);

      single_recon.SetInitialParameters(param_sets[i].model, param_sets[i].cam);
      if(iters_main_loop > 1) single_recon.SetIdentityPrior(identity_centroid);

      // Perform reconstruction
      {
        boost::timer::auto_cpu_timer t("Single image reconstruction finished in %w seconds.\n");
        single_recon.Reconstruct(opt_params);
      }

      // Store results
      auto tm = single_recon.GetGeometry();
      param_sets[i].mesh.UpdateVertices(tm);
      param_sets[i].model = single_recon.GetModelParameters();
      param_sets[i].indices = single_recon.GetIndices();
      param_sets[i].cam = single_recon.GetCameraParameters();

      if (true) {
        VisualizeReconstructionResult(step_single_recon_result_path, i);
        single_recon.SaveReconstructionResults( (step_single_recon_result_path / fs::path(to_string(i) + ".res")).string());
      }
    }

    // TODO Parameters estimation step, choose a consistent set of images for joint
    // optimization
    MatrixXd identity_weights(param_sets[0].model.Wid.rows(), num_images);
    for(int i=0;i<num_images;++i) {
      identity_weights.col(i) = param_sets[i].model.Wid;
    }

    identity_weights_history.push_back(identity_weights);

    // Remove outliers
    fs::path selection_result_path = step_result_path / fs::path("selection");
    safe_create(selection_result_path);

    const int selection_method = 1;
    switch(selection_method) {
      case 0: {
        const double ratios[] = {0.0, 0.2, 0.4, 0.6};
        consistent_set = StatsUtils::FindConsistentSet(identity_weights, 0.5, ratios[iters_main_loop] * num_images, &identity_centroid);
        assert(consistent_set.size() > 0);
        for(auto i : consistent_set) {
          VisualizeReconstructionResult(selection_result_path, i);
        }
        break;
      }
      case 1: {
        const double ratios[] = {0.0, 0.2, 0.4, 0.6};
        // Choose the ones with smallest error
        vector<pair<int, double>> errors(num_images);
        for(int i=0;i<num_images;++i) {
          errors[i] = make_pair(i, param_sets[i].stats.avg_error);
        }
        std::sort(errors.begin(), errors.end(), [](pair<int,double> a, pair<int, double> b){
          return a.second < b.second;
        });
        // Take the first few as good shape
        int k = max(1, static_cast<int>(ratios[iters_main_loop] * num_images));
        consistent_set.clear();
        for(int i=0;i<k;++i) {
          consistent_set.push_back(errors[i].first);
        }
        break;
      }
    }

    // Compute the centroid of the consistent set
    identity_centroid = VectorXd::Zero(param_sets[0].model.Wid.rows());
    for(auto i : consistent_set) {
      cout << i << endl;
      identity_centroid += param_sets[i].model.Wid;
    }
    identity_centroid /= consistent_set.size();

    // Update the identity weights for all images
    for(auto& param : param_sets) {
      param.model.Wid = identity_centroid;
    }

    // Joint reconstruction step, obtain refined identity weights
    int num_iters_joint_optimization = (iters_main_loop == max_iters_main_loop)?4:3;

    for(int iters_joint_optimization=0;
        iters_joint_optimization<num_iters_joint_optimization;
        ++iters_joint_optimization){
      // [Joint reconstruction] step 1: estimate pose and expression weights individually
      if((iters_joint_optimization == num_iters_joint_optimization - 1) && (iters_main_loop == max_iters_main_loop)) {
        // In the final iteration, no need to refine the identity weights anymore
        consistent_set.resize(num_images);
        for(int i=0;i<num_images;++i) consistent_set[i] = i;
      }

      fs::path joint_pre_result_path = step_result_path / fs::path("joint_recon_" + to_string(iters_joint_optimization) + "_pre");
      safe_create(joint_pre_result_path);

      for(auto i : consistent_set) {
        single_recon.SetMesh(param_sets[i].mesh);
        single_recon.SetIndices(param_sets[i].indices);
        single_recon.SetImageSize(param_sets[i].recon.imageWidth, param_sets[i].recon.imageHeight);
        single_recon.SetConstraints(param_sets[i].recon.cons);

        single_recon.SetInitialParameters(param_sets[i].model, param_sets[i].cam);
        single_recon.SetOptimizationMode(
          static_cast<typename SingleImageReconstructor<Constraint>::OptimizationMode>(
              SingleImageReconstructor<Constraint>::Pose
            | SingleImageReconstructor<Constraint>::Expression
            | SingleImageReconstructor<Constraint>::FocalLength));

        {
          boost::timer::auto_cpu_timer t("Single image reconstruction finished in %w seconds.\n");
          single_recon.Reconstruct(opt_params);
        }

        // Store results
        auto tm = single_recon.GetGeometry();
        param_sets[i].mesh.UpdateVertices(tm);
        param_sets[i].model = single_recon.GetModelParameters();
        param_sets[i].indices = single_recon.GetIndices();
        param_sets[i].cam = single_recon.GetCameraParameters();

        if (true) {
          // Visualize the reconstruction results
          VisualizeReconstructionResult(joint_pre_result_path, i);
        }
      }

      if((iters_joint_optimization == num_iters_joint_optimization - 1) && (iters_main_loop == max_iters_main_loop)) {
        // In the final iteration, no need to refine the identity weights anymore
        break;
      }

      // [Joint reconstruction] step 2: estimate identity weights jointly
      {
        fs::path joint_post_result_path = step_result_path / fs::path("joint_recon_" + to_string(iters_joint_optimization) + "_post");
        safe_create(joint_post_result_path);

        ceres::Problem problem;
        VectorXd params = param_sets[0].model.Wid;

        // Add constraints from each image
        for(auto i : consistent_set) {
          // Create a projected model first
          vector<MultilinearModel> model_projected_i(param_sets[i].indices.size());
          for(size_t j=0;j<param_sets[i].indices.size();++j) {
            model_projected_i[j] = model.project(vector<int>(1, param_sets[i].indices[j]));
            model_projected_i[j].ApplyWeights(param_sets[i].model.Wid, param_sets[i].model.Wexp);
          }

          // Create relevant matrices
          glm::dmat4 Rmat_i = glm::eulerAngleYXZ(param_sets[i].model.R[0], param_sets[i].model.R[1],
                                                 param_sets[i].model.R[2]);
          glm::dmat4 Tmat_i = glm::translate(glm::dmat4(1.0),
                                             glm::dvec3(param_sets[i].model.T[0],
                                                        param_sets[i].model.T[1],
                                                        param_sets[i].model.T[2]));
          glm::dmat4 Mview_i = Tmat_i * Rmat_i;

          double puple_distance = glm::distance(
            0.5 * (param_sets[i].recon.cons[28].data + param_sets[i].recon.cons[30].data),
            0.5 * (param_sets[i].recon.cons[32].data + param_sets[i].recon.cons[34].data));
          double weight_i = 100.0 / puple_distance;

          // Add per-vertex constraints
          for(size_t j=0;j<param_sets[i].indices.size();++j) {
            ceres::CostFunction * cost_function = new IdentityCostFunction_analytic(
              model_projected_i[j], param_sets[i].recon.cons[j], params.size(), Mview_i, Rmat_i,
              param_sets[i].cam, weight_i);

            problem.AddResidualBlock(cost_function, NULL, params.data());
          }
        }

        // Add prior constraint
        ceres::DynamicNumericDiffCostFunction<PriorCostFunction> *prior_cost_function =
          new ceres::DynamicNumericDiffCostFunction<PriorCostFunction>(
            new PriorCostFunction(prior.Wid_avg, prior.inv_sigma_Wid,
                                  prior.weight_Wid * consistent_set.size()));
        prior_cost_function->AddParameterBlock(params.size());
        prior_cost_function->SetNumResiduals(1);
        problem.AddResidualBlock(prior_cost_function, NULL, params.data());

        // Solve it
        {
          boost::timer::auto_cpu_timer timer_solve(
            "[Identity optimization] Problem solve time = %w seconds.\n");
          ceres::Solver::Options options;
          options.max_num_iterations = 3;
          options.minimizer_type = ceres::LINE_SEARCH;
          options.line_search_direction_type = ceres::LBFGS;
          DEBUG_EXPR(options.minimizer_progress_to_stdout = true;)
          ceres::Solver::Summary summary;
          Solve(options, &problem, &summary);
          DEBUG_OUTPUT(summary.FullReport())
        }

        // Update the identity weights
        for(auto& param : param_sets) {
          param.model.Wid = params;

          // Also update geometry if needed
          {
            model.ApplyWeights(param.model.Wid, param.model.Wexp);
            param.mesh.UpdateVertices(model.GetTM());
            param.mesh.ComputeNormals();
          }
        }

        for(auto i : consistent_set) {
          if(true) {
            VisualizeReconstructionResult(joint_post_result_path, i);
          }
        }

        identity_weights_centroid_history.push_back(params);
      }
    }
  } // end of main reconstruction loop

  // Output the reconstructed identity weights
  {
    for(size_t i=0;i<identity_weights_history.size();++i) {
      ofstream fout("identity_matrix" + std::to_string(i) + ".txt");
      fout << identity_weights_history[i];
      fout.close();
    }

    for(size_t i=0;i<identity_weights_centroid_history.size();++i) {
      ofstream fout("identity_centroid" + std::to_string(i) + ".txt");
      fout << identity_weights_centroid_history[i];
      fout.close();
    }
  }

  // Visualize the final reconstruction results
  for(int i=0;i<num_images;++i) {
    // Visualize the reconstruction results
    MeshVisualizer* w = new MeshVisualizer("reconstruction result", param_sets[i].mesh);
    w->BindConstraints(image_points_pairs[i].second);
    w->BindImage(image_points_pairs[i].first);
    w->BindLandmarks(init_indices);

    w->BindUpdatedLandmarks(param_sets[i].indices);
    w->SetMeshRotationTranslation(param_sets[i].model.R, param_sets[i].model.T);
    w->SetCameraParameters(param_sets[i].cam);

    int show_width = image_points_pairs[i].first.width();
    int show_height = image_points_pairs[i].first.height();
    double show_ratio = 640.0 / show_height;
    w->resize(show_width * show_ratio, 640);
    w->show();

    w->paintGL();
    QImage recon_image = w->grabFrameBuffer();
    fs::path image_path = fs::path(image_filenames[i]);

    recon_image.save( (result_path / fs::path(image_path.stem().string() + "_recon.png")).string().c_str() );

    ofstream fout(image_filenames[i] + ".res");
    fout << param_sets[i].cam << endl;
    fout << param_sets[i].model << endl;
    fout << param_sets[i].stats << endl;
    fout.close();
  }

  return true;
}

#endif //MULTILINEARRECONSTRUCTION_MULTIIMAGERECONSTRUCTOR_H
