#include "TFile.h"
#include "TH1.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TLegend.h"
#include "TString.h"
#include "TMath.h"

#include <cmath>
#include <iostream>

TGraph* make_roc(TH1* hsig, TH1* hbkg, bool high_score_signal, const char* name)
{
  int nb = hsig->GetNbinsX();

  double sig_tot = hsig->Integral(0, nb + 1);
  double bkg_tot = hbkg->Integral(0, nb + 1);

  auto* gr = new TGraph();
  gr->SetName(name);

  int p = 0;
  for (int ibin = 1; ibin <= nb + 1; ibin++) {
    double sig_pass = 0;
    double bkg_pass = 0;

    if (high_score_signal) {
      sig_pass = hsig->Integral(ibin, nb + 1);
      bkg_pass = hbkg->Integral(ibin, nb + 1);
    } else {
      sig_pass = hsig->Integral(0, ibin);
      bkg_pass = hbkg->Integral(0, ibin);
    }

    double tpr = sig_tot > 0 ? sig_pass / sig_tot : 0;
    double fpr = bkg_tot > 0 ? bkg_pass / bkg_tot : 0;

    gr->SetPoint(p++, fpr, tpr);
  }

  return gr;
}

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

void make_roc_multiclass()
{
  TFile* f = TFile::Open("hist_rootfiles/test_multiclass/test_run1_fhc_overlay_final.root");

  if (!f || f->IsZombie()) {
    std::cerr << "Could not open input ROOT file." << std::endl;
    return;
  }

  TH1* h_numu = (TH1*)f->Get("wwang_numu_numubar_FHC_numu_1_wwang_numu_numubar_BDT_all");
  TH1* h_numubar = (TH1*)f->Get("wwang_numu_numubar_FHC_numubar_1_wwang_numu_numubar_BDT_all");
  TH1* h_other = (TH1*)f->Get("wwang_numu_numubar_FHC_other_1_wwang_numu_numubar_BDT_all");

  if (!h_numu || !h_numubar || !h_other) {
    std::cerr << "Missing one or more histograms. Check names in ROOT file." << std::endl;
    std::cerr << "Available keys are:" << std::endl;
    f->ls();
    return;
  }

  TH1* h_bkg_numubar = (TH1*)h_numu->Clone("h_bkg_numubar");
  h_bkg_numubar->Add(h_other);

  TH1* h_bkg_other = (TH1*)h_numu->Clone("h_bkg_other");
  h_bkg_other->Add(h_numubar);

  TGraph* gr_numubar = make_roc(h_numubar, h_bkg_numubar, true, "roc_numubar_vs_rest");
  TGraph* gr_other = make_roc(h_other, h_bkg_other, false, "roc_other_vs_rest");

  double auc_numubar = graph_auc(gr_numubar);
  double auc_other = graph_auc(gr_other);

  std::cout << "AUC numubarCC vs rest = " << auc_numubar << std::endl;
  std::cout << "AUC other vs rest     = " << auc_other << std::endl;

  gr_numubar->SetLineColor(kRed);
  gr_numubar->SetLineWidth(2);

  gr_other->SetLineColor(kGreen + 2);
  gr_other->SetLineWidth(2);

  TCanvas* c = new TCanvas("c_roc", "ROC", 700, 600);

  gr_numubar->Draw("AL");
  gr_numubar->SetTitle("ROC from WC framework;False Positive Rate;True Positive Rate");
  gr_numubar->GetXaxis()->SetLimits(0, 1);
  gr_numubar->SetMinimum(0);
  gr_numubar->SetMaximum(1);

  gr_other->Draw("L SAME");

  TLegend* leg = new TLegend(0.18, 0.18, 0.68, 0.34);
  leg->AddEntry(gr_numubar, Form("#bar{#nu}_{#mu} CC vs rest, AUC = %.3f", auc_numubar), "l");
  leg->AddEntry(gr_other, Form("other vs rest, AUC = %.3f", auc_other), "l");
  leg->Draw();

  c->SaveAs("roc_multiclass_cpp.png");
}