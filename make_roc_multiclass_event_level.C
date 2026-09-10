#include "TFile.h"
#include "TTree.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TLegend.h"
#include "TString.h"
#include "TStyle.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

struct EventScore {
  int truth_class;
  double score;
  double weight;
};

double graph_auc(TGraph* gr)
{
  double auc = 0.0;
  int n = gr->GetN();

  for (int i = 1; i < n; i++) {
    double x1, y1, x2, y2;
    gr->GetPoint(i - 1, x1, y1);
    gr->GetPoint(i, x2, y2);

    auc += 0.5 * (y1 + y2) * (x2 - x1);
  }

  return std::abs(auc);
}

TGraph* make_one_vs_rest_roc(
    std::vector<EventScore> events,
    int signal_class,
    bool use_weights,
    const char* graph_name)
{
  std::sort(events.begin(), events.end(),
            [](const EventScore& a, const EventScore& b) {
              return a.score > b.score;
            });

  double total_sig = 0.0;
  double total_bkg = 0.0;

  for (const auto& ev : events) {
    double w = use_weights ? ev.weight : 1.0;
    if (ev.truth_class == signal_class) total_sig += w;
    else total_bkg += w;
  }

  auto* gr = new TGraph();
  gr->SetName(graph_name);

  int p = 0;
  double tp = 0.0;
  double fp = 0.0;

  gr->SetPoint(p++, 0.0, 0.0);

  Long64_t i = 0;
  Long64_t n = events.size();

  while (i < n) {
    double threshold = events[i].score;

    while (i < n && events[i].score == threshold) {
      double w = use_weights ? events[i].weight : 1.0;

      if (events[i].truth_class == signal_class) tp += w;
      else fp += w;

      i++;
    }

    double tpr = total_sig > 0.0 ? tp / total_sig : 0.0;
    double fpr = total_bkg > 0.0 ? fp / total_bkg : 0.0;

    gr->SetPoint(p++, fpr, tpr);
  }

  return gr;
}

void make_roc_multiclass_event_level()
{
  const char* input_file =
    "hist_rootfiles/test_multiclass/test_run1_fhc_overlay_with_roc.root";

  const char* tree_name = "wwang_roc";

  // Your Python roc_curve call did not pass sample_weight,
  // so false means this is the direct Python-equivalent unweighted ROC.
  bool use_weights = false;

  TFile* f = TFile::Open(input_file);
  if (!f || f->IsZombie()) {
    std::cerr << "Could not open input file: " << input_file << std::endl;
    return;
  }

  TTree* t = (TTree*)f->Get(tree_name);
  if (!t) {
    std::cerr << "Could not find tree: " << tree_name << std::endl;
    f->ls();
    return;
  }

  Int_t truth_class = -1;
  Float_t p_others = -999.;
  Float_t p_numu = -999.;
  Float_t p_numubar = -999.;
  Double_t weight = 1.0;

  t->SetBranchAddress("truth_class", &truth_class);
  t->SetBranchAddress("p_others", &p_others);
  t->SetBranchAddress("p_numu", &p_numu);
  t->SetBranchAddress("p_numubar", &p_numubar);

  if (t->GetBranch("weight")) {
    t->SetBranchAddress("weight", &weight);
  }

  std::vector<EventScore> events_others;
  std::vector<EventScore> events_numu;
  std::vector<EventScore> events_numubar;

  Long64_t nentries = t->GetEntries();

  for (Long64_t i = 0; i < nentries; i++) {
    t->GetEntry(i);

    if (truth_class < 0 || truth_class > 2) continue;
    if (!std::isfinite(p_others)) continue;
    if (!std::isfinite(p_numu)) continue;
    if (!std::isfinite(p_numubar)) continue;

    events_others.push_back({truth_class, p_others, weight});
    events_numu.push_back({truth_class, p_numu, weight});
    events_numubar.push_back({truth_class, p_numubar, weight});
  }

  TGraph* gr_others =
    make_one_vs_rest_roc(events_others, 0, use_weights, "roc_others");

  TGraph* gr_numu =
    make_one_vs_rest_roc(events_numu, 1, use_weights, "roc_numu");

  TGraph* gr_numubar =
    make_one_vs_rest_roc(events_numubar, 2, use_weights, "roc_numubar");

  double auc_others = graph_auc(gr_others);
  double auc_numu = graph_auc(gr_numu);
  double auc_numubar = graph_auc(gr_numubar);

  std::cout << "AUC Others vs rest    = " << auc_others << std::endl;
  std::cout << "AUC numuCC vs rest    = " << auc_numu << std::endl;
  std::cout << "AUC numubarCC vs rest = " << auc_numubar << std::endl;

  gr_others->SetLineColor(kGreen + 2);
  gr_numu->SetLineColor(kBlue);
  gr_numubar->SetLineColor(kRed);

  gr_others->SetLineWidth(2);
  gr_numu->SetLineWidth(2);
  gr_numubar->SetLineWidth(2);

  gStyle->SetOptStat(0);

  TCanvas* c = new TCanvas("c_roc", "Multiclass ROC", 850, 650);

  gr_others->Draw("AL");
  gr_others->SetTitle("Multiclass ROC Curves from C++;False Positive Rate;True Positive Rate");
  gr_others->GetXaxis()->SetLimits(0.0, 1.0);
  gr_others->SetMinimum(0.0);
  gr_others->SetMaximum(1.05);

  gr_numu->Draw("L SAME");
  gr_numubar->Draw("L SAME");

  TLegend* leg = new TLegend(0.48, 0.18, 0.88, 0.36);
  leg->AddEntry(gr_others, Form("Others, AUC = %.3f", auc_others), "l");
  leg->AddEntry(gr_numu, Form("#nu_{#mu} CC, AUC = %.3f", auc_numu), "l");
  leg->AddEntry(gr_numubar, Form("#bar{#nu}_{#mu} CC, AUC = %.3f", auc_numubar), "l");
  leg->Draw();

  c->SaveAs("roc_multiclass_like_python_cpp.png");
}
