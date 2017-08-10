//
// barre de LED
//
LEDlargeur=51;
LEDprofondeur=10.2;
LEDhauteur=3.2;


module led(extra=[0,0,0],shift=[0,0,0]) {
    translate([-LEDlargeur/2,-LEDprofondeur/2,0])
    translate(-extra/2+shift)
    cube([LEDlargeur,LEDprofondeur,LEDhauteur]+extra);
}


module LEDpiece(tol=[0.6,0.6,0.5]) {
    color("green") led(shift=[0,0,4-LEDhauteur-tol[2]/2]);
}

module LEDsupport(tol=[0.6,0.6,0.5]) {
    difference() {
        translate([0,0,2]) cube([70,30,4],center=true);
        led(extra=[-2,-4,10]+tol,shift=[1,0,0]);
*        led(extra=[-40,0,10]+tol,shift=[-40/2,0,0]); // special!!
        led(extra=[-40,0,10]+tol,shift=[40/2,0,0]);
        led(extra=[0,0,1]+tol,shift=[0,0,4-LEDhauteur+1/2]);
    }
}

translate([0,-40,0]) {
    LEDsupport();
    *LEDpiece();
}


//
// NFC
//
NFClargeur=59.7;
NFCprofondeur=39.4;
NFChauteur=1.2;

module nfc(extra=[0,0,0],shift=[0,0,0]) {
    translate([-NFClargeur/2+NFCprofondeur/2,0,0])
    translate([-NFClargeur/2,-NFCprofondeur/2,0])
    translate(-extra/2+shift)
    cube([NFClargeur,NFCprofondeur,NFChauteur]+extra);
}


module NFCpiece(tol=[0.6,0.6,2]) {
    color("green") nfc(shift=[0,0,4-NFChauteur-tol[2]/2]);
}


module NFCsupport(tol=[0.6,0.6,2]) {
    difference() {
        translate([-NFClargeur/2+NFCprofondeur/2,0,0])
        translate([0,0,2]) cube([80,60,4],center=true);
        nfc(extra=[-2,-4,10]+tol,shift=[-1,0,0]);
        nfc(extra=[-40,0,10]+tol,shift=[-40/2,0,0]);
        nfc(extra=[0,0,1]+tol,shift=[0,0,4-NFChauteur+1/2]);
    }
}

* union() {
*%NFCpiece();
NFCsupport();
}


//
// poidsA
// attache du dessus
//

POIDSespace=7; // minimum 6 pour la tete de vis
epaisseur=2;
barreW=25;extraW=5;
barreH=13;extraH=5;

module triple() {
    for(i=[0:120:359]) {
        hull() {
            rotate([0,0,i]) children(0);
            children(1);
        }
    }
}

module separe(d=[15,0,0]) {
    translate(-d/2) children();
    translate(d/2) children();
}

module poidsA(helice=true,extraTrou=0) {
    difference() {
        union() {
            if( helice ) {
                triple() {
                    translate([38,0,-1]) cylinder(d=15,h=1);
                    translate([0,0,-POIDSespace]) cylinder(d=10,h=POIDSespace);
                }
            }
            translate([0,0,-POIDSespace/2]) cube([barreW+extraW,barreH+extraH,POIDSespace],center=true);
        }
        // les deux trous
        separe() {
            translate([0,0,50-POIDSespace+epaisseur]) cylinder(d=7.5+extraTrou*2,h=100,center=true,$fn=32);
            cylinder(d=4+extraTrou,h=100,center=true,$fn=32);
        }
    }
}

module poidsB() {
    translate([80-barreW,0,-POIDSespace*2-12.75]) rotate([180,0,0]) poidsA(helice=false,extraTrou=1);
}

module poidsBarre() {
    color("red")
    translate([40-barreW/2,0,-12.75/2-POIDSespace])
    cube([80,12.75,12.75],center=true);
}

*poidsBarre();
*%cylinder(d=120,h=4);
poidsA();
*poidsB();


module deplace() {
    translate([0,0,-POIDSespace*2-12.75]) children();
}





