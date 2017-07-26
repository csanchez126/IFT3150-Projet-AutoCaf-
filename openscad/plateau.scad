

$fn=64;

use <composants.scad>;

module masque() {
    hull() { children(); }
}



module base() {
    render()
    difference() {
        cylinder(d=120,h=4);
        masque() NFCsupport();
        masque() translate([0,-45,0]) LEDsupport();

    }

    NFCsupport();
    intersection() {
        cylinder(d=120,h=20,center=true);
        translate([0,-45,0]) LEDsupport();
    }

}

module insere() {
        difference() {
            children(0);
            hull() scale([1,1,1.01]) children(1);
        }
        children(1);
}

module baseBas() {
    difference() {
    cylinder(d=160,h=3);
    for(i=[0:55:300]) {
        rotate([0,0,i+30+10])
        translate([50,0,0])
        cylinder(d=40,h=100,center=true);
    }
    cylinder(d=40,h=100,center=true);
} 
}

insere() {
    deplace() baseBas();
    poidsB();
}

base();

poidsA();
poidsBarre();
