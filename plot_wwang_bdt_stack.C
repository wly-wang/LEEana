void plot_wwang_bdt_stack(
  const char* infile="hist_rootfiles/test_multiclass/test_run1_fhc_overlay_final.root",
  const char* outprefix="hist_rootfiles/test_multiclass/test_run1_fhc_overlay_final_stack"
) {
  TFile f(infile);

  TH1 *h_numu = (TH1*)f.Get("wwang_numu_numubar_FHC_numu_1_wwang_numu_numubar_BDT_all");
  TH1 *h_numubar = (TH1*)f.Get("wwang_numu_numubar_FHC_numubar_1_wwang_numu_numubar_BDT_all");
  TH1 *h_other = (TH1*)f.Get("wwang_numu_numubar_FHC_other_1_wwang_numu_numubar_BDT_all");

  if (!h_numu || !h_numubar || !h_other) {
    std::cout << "Missing histogram. File contains:" << std::endl;
    f.ls();
    return;
  }

  h_numu = (TH1*)h_numu->Clone("h_numu");
  h_numubar = (TH1*)h_numubar->Clone("h_numubar");
  h_other = (TH1*)h_other->Clone("h_other");

  h_other->SetFillColor(kGreen+2);
  h_numu->SetFillColor(kBlue);
  h_numubar->SetFillColor(kRed);

  h_other->SetLineColor(kGreen+2);
  h_numu->SetLineColor(kBlue);
  h_numubar->SetLineColor(kRed);

  THStack *hs = new THStack(
    "wwang_bdt_stack",
    "BDT Score Distribution;BDT Score (-3 #rightarrow others, -1 #rightarrow #nu_{#mu}CC, +1 #rightarrow #bar{#nu}_{#mu}CC);Events"
  );

  hs->Add(h_other);
  hs->Add(h_numu);
  hs->Add(h_numubar);

  TCanvas *c = new TCanvas("c_wwang_bdt_stack", "c_wwang_bdt_stack", 900, 700);
  hs->Draw("hist");

  TLegend *leg = new TLegend(0.62, 0.72, 0.88, 0.88);
  leg->AddEntry(h_other, "Background - Others", "f");
  leg->AddEntry(h_numu, "Signal - #nu_{#mu}CC", "f");
  leg->AddEntry(h_numubar, "Signal - #bar{#nu}_{#mu}CC", "f");
  leg->Draw();

  c->SaveAs(Form("%s.png", outprefix));
  c->SaveAs(Form("%s.pdf", outprefix));

  std::cout << "numu entries=" << h_numu->GetEntries()
            << " integral=" << h_numu->Integral(0, h_numu->GetNbinsX()+1) << std::endl;
  std::cout << "numubar entries=" << h_numubar->GetEntries()
            << " integral=" << h_numubar->Integral(0, h_numubar->GetNbinsX()+1) << std::endl;
  std::cout << "other entries=" << h_other->GetEntries()
            << " integral=" << h_other->Integral(0, h_other->GetNbinsX()+1) << std::endl;
}
