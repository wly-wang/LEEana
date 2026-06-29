#!/usr/bin/perl
my $num1 = scalar(@ARGV);

for (my $i=1;$i<18;$i++){
    if ($i% 9 == 8){
	if ($num1==0){
	    system("./bin/xf_cov_matrix -r$i -bwwang_numu_numubar_BDT");
	}else{
	    print "Oscillation! \n"; 
	    system("./bin/xf_cov_matrix -r$i -o1 -bwwang_numu_numubar_BDT");
	}
    }else{
	if ($num1 ==0){
	    system("./bin/xf_cov_matrix -r$i -bwwang_numu_numubar_BDT&");
	}else{
	    print "Oscillation! \n"; 
	    system("./bin/xf_cov_matrix -r$i -o1 -bwwang_numu_numubar_BDT&");
	}
    }
}
