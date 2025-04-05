inr = [55.2, 25.5, 13.3];
alw = [.3, .3, .3];
thk = [2, 2, 2];

sp1 = [4.5, inr.y + 2*alw.y, 4];
sp2 = [6, inr.y + 2*alw.y, 3.3];
usb = [2*thk.x, 14, inr.z];

out = [inr.x + 2*(alw.x + thk.x),
       inr.y + 2*(alw.y + thk.y),
       inr.z + alw.z + thk.z];

hlw = [inr.x + alw.x, inr.y + alw.y, inr.z + alw.z + 1];
swh = [4, inr.y + 2*(.7 + alw.z), hlw.z];
cut = [1, out.y + .2, hlw.z];

difference() {
translate([for (i = [0:1:2]) -(alw[i] + thk[i])]) cube(out);
translate([for (i = [0:1:2]) -alw[i]]) cube(hlw);
translate([-thk.x*2, (inr.y - usb.y)/2, 5 - alw.z]) cube(usb);
translate([9.7, -(.7 + alw.y), 2]) cube(swh);
translate([8.8, -(.1 + thk.y + alw.y), 1]) cube(cut);
translate([12.8, -(.1 + thk.y + alw.y), 1]) cube(cut);
};
translate([for (i = [0:1:2]) -alw[i]]) cube(sp1);
translate([hlw.x - sp2.x, -alw.y, -alw.z]) cube(sp2);
