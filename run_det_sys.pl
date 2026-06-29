#!/usr/bin/perl
my $num1 = scalar(@ARGV);

for (my $i=1;$i<=10;$i++){
    if ($num1 ==0){
	if ($i==5) {
          print "Skip WireMod dEdx \n";
        }
	else {
	  system("./bin/det_cov_matrix -r$i -bwwang_numu_numubar_BDT&");
        }
    }else{
	print "Oscillation! \n"; 
	system("./bin/det_cov_matrix -r$i -o1 -bwwang_numu_numubar_BDT&");
    }
}
