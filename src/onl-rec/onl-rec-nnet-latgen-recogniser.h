// onl-rec/onl-rec-latgen-recogniser.h
/* Copyright (c) 2013, Ondrej Platek, Ufal MFF UK <oplatek@ufal.mff.cuni.cz>
 *               2012-2013  Vassil Panayotov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
 * WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
 * MERCHANTABLITY OR NON-INFRINGEMENT.
 * See the Apache 2 License for the specific language governing permissions and
 * limitations under the License. */
#ifndef KALDI_ONL_REC_NNET_LATGEN_RECOGNISER_H_
#define KALDI_ONL_REC_NNET_LATGEN_RECOGNISER_H_
#include <string>
#include <vector>
#include "base/kaldi-types.h"

namespace fst{
  // forward declarations - useful for interfacing the class from Python/Java
  // No need to include the headers
  template <typename Arc> class Fst;
  template <typename Weight> class ArcTpl; 
  template <class W> class TropicalWeightTpl;
  typedef TropicalWeightTpl<float> TropicalWeight;
  typedef ArcTpl<TropicalWeight> StdArc;
  typedef Fst<StdArc> StdFst;
  template <class W> class LogWeightTpl;
  typedef LogWeightTpl<float> LogWeight;
  typedef ArcTpl<LogWeight> LogArc;
  template<class FloatType> class LatticeWeightTpl;
  template <class A> class VectorFst;
  template<class WeightType, class IntType> class CompactLatticeWeightTpl; 

}
namespace kaldi{ 
  // forward declarations - useful for interfacing the class from Python/Java
  // No need to include the headers
  typedef fst::LatticeWeightTpl<BaseFloat> LatticeWeight;
  typedef fst::ArcTpl<LatticeWeight> LatticeArc;
  typedef fst::VectorFst<LatticeArc> Lattice;

  typedef fst::CompactLatticeWeightTpl<LatticeWeight, kaldi::int32> CompactLatticeWeight;
  typedef fst::ArcTpl<CompactLatticeWeight> CompactLatticeArc;
  typedef fst::VectorFst<CompactLatticeArc> CompactLattice;

  class OnlineNnet2FeaturePipeline;
  class OnlineNnet2FeaturePipelineInfo;
  namespace nnet2 {
    class DecodableNnet2Online;
    class AmNnet;
  }
  class TransitionModel;
  class AmDiagGmm;
  class LatticeFasterOnlineDecoder;
  struct OnlineNnetLatgenRecogniserConfig;
}


namespace kaldi {

/// \addtogroup online_latgen 
/// @{



class OnlineNnetLatgenRecogniser {
  public:
    OnlineNnetLatgenRecogniser(): pipe_(NULL), feature_info_(NULL),
      decodable_(NULL), trans_model_(NULL), am_(NULL),
      decoder_(NULL), hclg_(NULL), config_(NULL) { }

    virtual ~OnlineNnetLatgenRecogniser();
    size_t Decode(size_t max_frames);
    void FrameIn(unsigned char *frame, size_t frame_len);
    bool GetBestPath(std::vector<int> *v_out, BaseFloat *prob);
    bool GetLattice(fst::VectorFst<fst::LogArc> * out_fst, double *tot_lik, bool end_of_utt=true);
    void FinalizeDecoding();
    void Reset(bool reset_pipeline);
    bool Setup(int argc, char **argv);
  private:
    std::vector<int32> silence_phones_;
    OnlineNnet2FeaturePipeline *pipe_;
    OnlineNnet2FeaturePipelineInfo *feature_info_;
    nnet2::DecodableNnet2Online *decodable_;
    TransitionModel *trans_model_;
    nnet2::AmNnet *am_;
    LatticeFasterOnlineDecoder *decoder_;
    fst::StdFst *hclg_;
    OnlineNnetLatgenRecogniserConfig *config_;
    bool initialized_;
    void ResetPipeline();
    void Deallocate();
};

/// @} end of "addtogroup online_latgen"

} // namespace kaldi

#endif  // #ifdef KALDI_ONL_REC_NNET_LATGEN_RECOGNISER_H_
