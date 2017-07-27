


use <composants.scad>;

$fn=128;


module masque() {
    hull() { children(); }
}



module base(diam=130) {
    //render()
    difference() {
        cylinder(d=diam,h=4);
        masque() NFCsupport();
        masque() translate([0,-45,0]) LEDsupport();

    }

    NFCsupport();
    intersection() {
        cylinder(d=diam,h=20,center=true);
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

module baseBas(diam=160) {
    difference() {
        cylinder(d=diam,h=3);
        for(i=[-2:2]) {
            rotate([0,0,0+180+60*i])
                translate([50/160*diam,0,0])
                cylinder(d=45/160*diam,h=100,center=true);
        }
        for(i=[-3:3]) {
            rotate([0,0,0+180+60*i-30])
                translate([65/160*diam,0,0])
                cylinder(d=15/160*diam,h=100,center=true);
        }
        cylinder(d=45/160*diam,h=100,center=true);
    } 
}

//
// couvercle
//
module couvercle(topD=130,bottomD=160,H=22,ep=2) {
    difference() {
        hull() {
            cylinder(d=topD+ep+1,h=4+ep);
            translate([0,0,-H]) cylinder(d=bottomD+ep+1,h=1);
        }
        translate([0,0,-1]) cylinder(d=topD+1,h=4+1+0.25);
        hull() {
            cylinder(d=topD+1,h=2);
            translate([0,0,-H-0.1]) cylinder(d=bottomD,h=1);
        }
        translate([0,0,0.25]) children();
    }
    
}

module rebord(diam=160,extraD=25,extraH=4) {
    difference() {
        hull() {
            cylinder(d=diam,h=4+extraH);
            cylinder(d=diam+extraD,h=1);
        }
        cylinder(d=diam-4,h=30,center=true);
    }
}


// pour voir seulement la moitie de l'objet...
module cut() {
    intersection() {
        translate([-100,0,-100]) cube([200,200,200]);
        children();
    }
}

module flip() {
    rotate([180,0,0]) children();
}


//
// couvercle
//

//translate([0,0,40])
cut()
//flip()
color("purple") couvercle(ep=1) deplace() rebord(diam=160-10);

//
// plateau balance du haut
//

//flip()
union() {
    base(diam=130);
    poidsA();
}

// barre de la balance
%poidsBarre();

//
// plateau balance du bas
//
union() {
    insere() {
        deplace() baseBas(diam=160);
        poidsB();
    }
    deplace() rebord(diam=160-10);
}






