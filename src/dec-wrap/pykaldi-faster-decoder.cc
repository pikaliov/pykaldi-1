// pykaldi/pykaldi-faster-decoder.cc

// Copyright 2012 Cisco Systems (author: Matthias Paulik)

//   Modifications to the original contribution by Cisco Systems made by:
//   Vassil Panayotov
//   Ondrej Platek

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "pykaldi-faster-decoder.h"
#include "fstext/fstext-utils.h"
#include "hmm/hmm-utils.h"

namespace kaldi {

size_t
PykaldiFasterDecoder::Decode(DecodableInterface *decodable) {
  ProcessNonemitting(std::numeric_limits<float>::max());
  size_t processed;
  for (processed=0 ; !decodable->IsLastFrame(frame_ - 1);
       ++frame_, ++utt_frames_, ++processed) {
    BaseFloat weight_cutoff = ProcessEmitting(decodable, frame_);
    ProcessNonemitting(weight_cutoff);
  }
  KALDI_VLOG(1) << "DEBUG End of Decode";
  return processed;
}


void PykaldiFasterDecoder::ResetDecoder(bool full) {
  ClearToks(toks_.Clear());
  StateId start_state = fst_.Start();
  KALDI_ASSERT(start_state != fst::kNoStateId);
  Arc dummy_arc(0, 0, Weight::One(), start_state);
  Token *dummy_token = new Token(dummy_arc, NULL);
  toks_.Insert(start_state, dummy_token);
  prev_immortal_tok_ = immortal_tok_ = dummy_token;
  utt_frames_ = 0;
  if (full)
    frame_ = 0;
}

void
PykaldiFasterDecoder::MakeLattice(const Token *start,
                                 const Token *end,
                                 fst::MutableFst<LatticeArc> *out_fst) const {
  out_fst->DeleteStates();
  if (start == NULL) return;
  bool is_final = false;
  Weight this_weight = Times(start->weight_,
                             fst_.Final(start->arc_.nextstate));
  if (this_weight != Weight::Zero())
    is_final = true;
  std::vector<LatticeArc> arcs_reverse;  // arcs in reverse order.
  for (const Token *tok = start; tok != end; tok = tok->prev_) {
    BaseFloat tot_cost = tok->weight_.Value() -
        (tok->prev_ ? tok->prev_->weight_.Value() : 0.0),
        graph_cost = tok->arc_.weight.Value(),
        ac_cost = tot_cost - graph_cost;
    LatticeArc l_arc(tok->arc_.ilabel,
                     tok->arc_.olabel,
                     LatticeWeight(graph_cost, ac_cost),
                     tok->arc_.nextstate);
    arcs_reverse.push_back(l_arc);
  }
  if(arcs_reverse.back().nextstate == fst_.Start()) {
    arcs_reverse.pop_back();  // that was a "fake" token... gives no info.
  }
  StateId cur_state = out_fst->AddState();
  out_fst->SetStart(cur_state);
  for (ssize_t i = static_cast<ssize_t>(arcs_reverse.size())-1; i >= 0; i--) {
    LatticeArc arc = arcs_reverse[i];
    arc.nextstate = out_fst->AddState();
    out_fst->AddArc(cur_state, arc);
    cur_state = arc.nextstate;
  }
  if (is_final) {
    Weight final_weight = fst_.Final(start->arc_.nextstate);
    out_fst->SetFinal(cur_state, LatticeWeight(final_weight.Value(), 0.0));
  } else {
    out_fst->SetFinal(cur_state, LatticeWeight::One());
  }
  RemoveEpsLocal(out_fst);
}


void PykaldiFasterDecoder::UpdateImmortalToken() {
  std::tr1::unordered_set<Token*> emitting;
  for (Elem *e = toks_.GetList(); e != NULL; e = e->tail) {
    Token* tok = e->val;
    while (tok->arc_.ilabel == 0) //deal with non-emitting ones ...
      tok = tok->prev_;
    if (tok != NULL)
      emitting.insert(tok);
  }
  Token* the_one = NULL;
  while (1) {
    if (emitting.size() == 1) {
      the_one = *(emitting.begin());
      break;
    }
    if (emitting.size() == 0)
      break;
    std::tr1::unordered_set<Token*> prev_emitting;
    std::tr1::unordered_set<Token*>::iterator it;
    for (it = emitting.begin(); it != emitting.end(); ++it) {
      Token* tok = *it;
      Token* prev_token = tok->prev_;
      while ((prev_token != NULL) && (prev_token->arc_.ilabel == 0))
        prev_token = prev_token->prev_; //deal with non-emitting ones
      if (prev_token == NULL)
        continue;
      prev_emitting.insert(prev_token);
    } // for
    emitting = prev_emitting;
  } // while
  if (the_one != NULL) {
    prev_immortal_tok_ = immortal_tok_;
    immortal_tok_ = the_one;
    return;
  }
}


bool
PykaldiFasterDecoder::PartialTraceback(fst::MutableFst<LatticeArc> *out_fst) {
  UpdateImmortalToken();
  if(immortal_tok_ == prev_immortal_tok_)
    return false; //no partial traceback at that point of time
  MakeLattice(immortal_tok_, prev_immortal_tok_, out_fst);
  return true;
}


void
PykaldiFasterDecoder::FinishTraceBack(fst::MutableFst<LatticeArc> *out_fst) {
  Token *best_tok = NULL;
  bool is_final = ReachedFinal();
  if (!is_final) {
    for (Elem *e = toks_.GetList(); e != NULL; e = e->tail)
      if (best_tok == NULL || *best_tok < *(e->val) )
        best_tok = e->val;
  } else {
    Weight best_weight = Weight::Zero();
    for (Elem *e = toks_.GetList(); e != NULL; e = e->tail) {
      Weight this_weight = Times(e->val->weight_, fst_.Final(e->key));
      if (this_weight != Weight::Zero() &&
          this_weight.Value() < best_weight.Value()) {
        best_weight = this_weight;
        best_tok = e->val;
      }
    }
  }
  MakeLattice(best_tok, immortal_tok_, out_fst);
}


void
PykaldiFasterDecoder::TracebackNFrames(int32 nframes,
                                      fst::MutableFst<LatticeArc> *out_fst) {
  Token *best_tok = NULL;
  for (Elem *e = toks_.GetList(); e != NULL; e = e->tail)
    if (best_tok == NULL || *best_tok < *(e->val) )
      best_tok = e->val;
  if (best_tok == NULL) {
    out_fst->DeleteStates();
    return;
  }

  bool is_final = false;
  Weight this_weight = Times(best_tok->weight_,
                             fst_.Final(best_tok->arc_.nextstate));
  if (this_weight != Weight::Zero())
    is_final = true;
  std::vector<LatticeArc> arcs_reverse;  // arcs in reverse order.
  for (Token *tok = best_tok; (tok != NULL) && (nframes > 0); tok = tok->prev_) {
    if (tok->arc_.ilabel != 0) // count only the non-epsilon arcs
      --nframes;
    BaseFloat tot_cost = tok->weight_.Value() -
                             (tok->prev_ ? tok->prev_->weight_.Value() : 0.0);
    BaseFloat graph_cost = tok->arc_.weight.Value();
    BaseFloat ac_cost = tot_cost - graph_cost;
    LatticeArc larc(tok->arc_.ilabel,
                     tok->arc_.olabel,
                     LatticeWeight(graph_cost, ac_cost),
                     tok->arc_.nextstate);
    arcs_reverse.push_back(larc);
  }
  if(arcs_reverse.back().nextstate == fst_.Start())
    arcs_reverse.pop_back();  // that was a "fake" token... gives no info.
  StateId cur_state = out_fst->AddState();
  out_fst->SetStart(cur_state);
  for (ssize_t i = static_cast<ssize_t>(arcs_reverse.size())-1; i >= 0; i--) {
    LatticeArc arc = arcs_reverse[i];
    arc.nextstate = out_fst->AddState();
    out_fst->AddArc(cur_state, arc);
    cur_state = arc.nextstate;
  }
  if (is_final) {
    Weight final_weight = fst_.Final(best_tok->arc_.nextstate);
    out_fst->SetFinal(cur_state, LatticeWeight(final_weight.Value(), 0.0));
  } else {
    out_fst->SetFinal(cur_state, LatticeWeight::One());
  }
  RemoveEpsLocal(out_fst);
}


bool PykaldiFasterDecoder::EndOfUtterance() {
  fst::VectorFst<LatticeArc> trace;
  int32 sil_frm = opts_.inter_utt_sil / (1 + utt_frames_ / opts_.max_utt_len);
  TracebackNFrames(sil_frm, &trace);
  std::vector<int32> isymbols;
  fst::GetLinearSymbolSequence(trace, &isymbols,
                               static_cast<std::vector<int32>* >(0),
                               static_cast<LatticeArc::Weight*>(0));
  std::vector<std::vector<int32> > split;
  SplitToPhones(trans_model_, isymbols, &split);
  for (size_t i = 0; i < split.size(); i++) {
    int32 tid = split[i][0];
    int32 phone = trans_model_.TransitionIdToPhone(tid);
    if (silence_set_.count(phone) == 0)
      return false;
  }
  return true;
}



} // namespace kaldi
