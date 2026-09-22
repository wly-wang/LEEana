#include "TAxis.h"
#include "TCanvas.h"
#include "TFile.h"
#include "TGraph.h"
#include "TH1D.h"
#include "THStack.h"
#include "TLegend.h"
#include "TLine.h"
#include "TString.h"
#include "TStyle.h"
#include "TTree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

struct RocEvent {
  int truth_class;
  double response;
  double weight;
};

TGraph* make_one_vs_rest_roc(std::vector<RocEvent> events,
                             int signal_class,
                             const char* graph_name)
{
  std::sort(events.begin(), events.end(),
            [](const RocEvent& a, const RocEvent& b) {
              return a.response > b.response;
            });

  double total_signal = 0.0;
  double total_background = 0.0;
  for (const auto& event : events) {
    if (event.truth_class == signal_class) total_signal += event.weight;
    else total_background += event.weight;
  }

  auto* graph = new TGraph();
  graph->SetName(graph_name);
  graph->SetPoint(0, 0.0, 0.0);

  double true_positive = 0.0;
  double false_positive = 0.0;
  int point = 1;

  // Each unique response is a threshold, matching sklearn.metrics.roc_curve.
  std::size_t i = 0;
  while (i < events.size()) {
    const double threshold = events[i].response;
    while (i < events.size() && events[i].response == threshold) {
      if (events[i].truth_class == signal_class) {
        true_positive += events[i].weight;
      } else {
        false_positive += events[i].weight;
      }
      ++i;
    }

    const double tpr = total_signal > 0.0
                         ? true_positive / total_signal
                         : 0.0;
    const double fpr = total_background > 0.0
                         ? false_positive / total_background
                         : 0.0;
    graph->SetPoint(point++, fpr, tpr);
  }

  return graph;
}

double graph_auc(const TGraph* graph)
{
  double auc = 0.0;
  for (int i = 1; i < graph->GetN(); ++i) {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    graph->GetPoint(i - 1, x0, y0);
    graph->GetPoint(i, x1, y1);
    auc += 0.5 * (y0 + y1) * (x1 - x0);
  }
  return auc;
}

}  // namespace

void plot_wwang_bdt_validation(
    const char* input_file =
      "hist_rootfiles/test_multiclass/test_run1_fhc_overlay_dart_parity.root",
    const char* output_prefix = "run1_fhc_dart",
    bool use_weights = false)
{
  gStyle->SetOptStat(0);

  TFile* input = TFile::Open(input_file, "READ");
  if (!input || input->IsZombie()) {
    std::cerr << "Could not open input file: " << input_file << std::endl;
    return;
  }

  auto* tree = dynamic_cast<TTree*>(input->Get("wwang_roc"));
  if (!tree) {
    std::cerr << "Could not find TTree 'wwang_roc' in " << input_file
              << std::endl;
    input->ls();
    return;
  }

  const std::array<const char*, 6> required_branches = {
    "truth_class", "score", "p_others", "p_numu", "p_numubar", "weight"
  };
  for (const char* name : required_branches) {
    if (!tree->GetBranch(name)) {
      std::cerr << "Missing required branch: " << name << std::endl;
      return;
    }
  }

  Int_t truth_class = -1;
  Float_t score = -999.0f;
  Float_t p_others = -999.0f;
  Float_t p_numu = -999.0f;
  Float_t p_numubar = -999.0f;
  Double_t event_weight = 1.0;

  tree->SetBranchAddress("truth_class", &truth_class);
  tree->SetBranchAddress("score", &score);
  tree->SetBranchAddress("p_others", &p_others);
  tree->SetBranchAddress("p_numu", &p_numu);
  tree->SetBranchAddress("p_numubar", &p_numubar);
  tree->SetBranchAddress("weight", &event_weight);

  constexpr int number_of_bins = 35;
  constexpr double score_minimum = -3.0;
  constexpr double score_maximum = 1.0;

  auto* h_others = new TH1D("h_score_others", "", number_of_bins,
                            score_minimum, score_maximum);
  auto* h_numu = new TH1D("h_score_numu", "", number_of_bins,
                          score_minimum, score_maximum);
  auto* h_numubar = new TH1D("h_score_numubar", "", number_of_bins,
                             score_minimum, score_maximum);
  h_others->SetDirectory(nullptr);
  h_numu->SetDirectory(nullptr);
  h_numubar->SetDirectory(nullptr);

  h_others->SetFillColor(kGreen + 2);
  h_numu->SetFillColor(kBlue);
  h_numubar->SetFillColor(kRed);
  h_others->SetLineColor(kGreen + 2);
  h_numu->SetLineColor(kBlue);
  h_numubar->SetLineColor(kRed);

  std::vector<RocEvent> roc_others;
  std::vector<RocEvent> roc_numu;
  std::vector<RocEvent> roc_numubar;
  std::array<Long64_t, 3> class_entries = {0, 0, 0};
  Long64_t skipped_entries = 0;

  const Long64_t number_of_entries = tree->GetEntries();
  roc_others.reserve(number_of_entries);
  roc_numu.reserve(number_of_entries);
  roc_numubar.reserve(number_of_entries);

  for (Long64_t entry = 0; entry < number_of_entries; ++entry) {
    tree->GetEntry(entry);

    if (truth_class < 0 || truth_class > 2 ||
        !std::isfinite(score) ||
        !std::isfinite(p_others) ||
        !std::isfinite(p_numu) ||
        !std::isfinite(p_numubar) ||
        (use_weights && !std::isfinite(event_weight))) {
      ++skipped_entries;
      continue;
    }

    const double weight = use_weights ? event_weight : 1.0;
    if (truth_class == 0) h_others->Fill(score, weight);
    if (truth_class == 1) h_numu->Fill(score, weight);
    if (truth_class == 2) h_numubar->Fill(score, weight);
    ++class_entries[truth_class];

    roc_others.push_back({truth_class, p_others, weight});
    roc_numu.push_back({truth_class, p_numu, weight});
    roc_numubar.push_back({truth_class, p_numubar, weight});
  }

  if (roc_others.empty()) {
    std::cerr << "No valid entries were found in wwang_roc." << std::endl;
    return;
  }

  auto* graph_others =
    make_one_vs_rest_roc(roc_others, 0, "roc_others_vs_rest");
  auto* graph_numu =
    make_one_vs_rest_roc(roc_numu, 1, "roc_numu_vs_rest");
  auto* graph_numubar =
    make_one_vs_rest_roc(roc_numubar, 2, "roc_numubar_vs_rest");

  const double auc_others = graph_auc(graph_others);
  const double auc_numu = graph_auc(graph_numu);
  const double auc_numubar = graph_auc(graph_numubar);

  std::cout << "Input wwang_roc entries = " << number_of_entries << '\n'
            << "Valid entries           = " << roc_others.size() << '\n'
            << "Skipped entries         = " << skipped_entries << '\n'
            << "Others entries          = " << class_entries[0] << '\n'
            << "numuCC entries          = " << class_entries[1] << '\n'
            << "numubarCC entries       = " << class_entries[2] << '\n'
            << std::fixed << std::setprecision(6)
            << "AUC Others vs rest      = " << auc_others << '\n'
            << "AUC numuCC vs rest      = " << auc_numu << '\n'
            << "AUC numubarCC vs rest   = " << auc_numubar << std::endl;

  std::ofstream summary(Form("%s_summary.txt", output_prefix));
  summary << "input_file=" << input_file << '\n'
          << "use_weights=" << (use_weights ? "true" : "false") << '\n'
          << "input_entries=" << number_of_entries << '\n'
          << "valid_entries=" << roc_others.size() << '\n'
          << "skipped_entries=" << skipped_entries << '\n'
          << "others_entries=" << class_entries[0] << '\n'
          << "numuCC_entries=" << class_entries[1] << '\n'
          << "numubarCC_entries=" << class_entries[2] << '\n'
          << std::fixed << std::setprecision(6)
          << "auc_others_vs_rest=" << auc_others << '\n'
          << "auc_numuCC_vs_rest=" << auc_numu << '\n'
          << "auc_numubarCC_vs_rest=" << auc_numubar << '\n';
  summary.close();

  auto* stack = new THStack(
    "score_stack",
    "BDT Score Distribution;"
    "BDT Score (-3 #rightarrow others, -1 #rightarrow #nu_{#mu}CC, "
    "+1 #rightarrow #bar{#nu}_{#mu}CC);Events"
  );
  stack->Add(h_others);
  stack->Add(h_numu);
  stack->Add(h_numubar);

  auto* score_canvas = new TCanvas("c_score_stack", "BDT score", 900, 700);
  score_canvas->SetLeftMargin(0.12);
  score_canvas->SetBottomMargin(0.12);
  stack->Draw("HIST");
  stack->GetXaxis()->SetNdivisions(510);

  auto* score_legend = new TLegend(0.62, 0.71, 0.88, 0.88);
  score_legend->AddEntry(h_others, "Background - Others", "f");
  score_legend->AddEntry(h_numu, "Signal - #nu_{#mu}CC", "f");
  score_legend->AddEntry(h_numubar, "Signal - #bar{#nu}_{#mu}CC", "f");
  score_legend->Draw();

  score_canvas->SaveAs(Form("%s_score_stack.png", output_prefix));
  score_canvas->SaveAs(Form("%s_score_stack.pdf", output_prefix));

  graph_others->SetLineColor(kGreen + 2);
  graph_numu->SetLineColor(kBlue);
  graph_numubar->SetLineColor(kRed);
  graph_others->SetLineWidth(2);
  graph_numu->SetLineWidth(2);
  graph_numubar->SetLineWidth(2);

  auto* roc_canvas = new TCanvas("c_roc", "Multiclass ROC", 850, 650);
  roc_canvas->SetGrid();
  roc_canvas->SetLeftMargin(0.12);
  roc_canvas->SetBottomMargin(0.12);

  graph_others->SetTitle(
    "One-vs-Rest ROC Curves;False Positive Rate;True Positive Rate"
  );
  graph_others->Draw("AL");
  graph_others->GetXaxis()->SetLimits(0.0, 1.0);
  graph_others->SetMinimum(0.0);
  graph_others->SetMaximum(1.05);
  graph_numu->Draw("L SAME");
  graph_numubar->Draw("L SAME");

  auto* diagonal = new TLine(0.0, 0.0, 1.0, 1.0);
  diagonal->SetLineColor(kBlack);
  diagonal->SetLineStyle(2);
  diagonal->Draw("SAME");

  auto* roc_legend = new TLegend(0.50, 0.18, 0.88, 0.37);
  roc_legend->AddEntry(
    graph_others, Form("Others, AUC = %.3f", auc_others), "l"
  );
  roc_legend->AddEntry(
    graph_numu, Form("#nu_{#mu}CC, AUC = %.3f", auc_numu), "l"
  );
  roc_legend->AddEntry(
    graph_numubar, Form("#bar{#nu}_{#mu}CC, AUC = %.3f", auc_numubar), "l"
  );
  roc_legend->Draw();

  roc_canvas->SaveAs(Form("%s_roc.png", output_prefix));
  roc_canvas->SaveAs(Form("%s_roc.pdf", output_prefix));

  TFile output(Form("%s_validation.root", output_prefix), "RECREATE");
  h_others->Write();
  h_numu->Write();
  h_numubar->Write();
  stack->Write();
  graph_others->Write();
  graph_numu->Write();
  graph_numubar->Write();
  score_canvas->Write();
  roc_canvas->Write();
  output.Close();

  input->Close();
}
