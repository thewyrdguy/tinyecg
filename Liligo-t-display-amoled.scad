inr = [55.2, 25.5, 13.3];
alw = [.3, .3, .3];
thk = [2, 2, 2];
le = 1.5;
te = 1.5;
be = 1.5;
re = 7.2;
out = [for (i = [0:1:2]) inr[i] + 2*(alw[i] + thk[i])];
hlw = [for (i = [0:1:2]) inr[i] + 2*alw[i]];
wnd = [inr.x-le-re, inr.y-te-be, 10];
difference() {
cube(out,center=true);
translate([-thk.x,0,0]) cube(hlw,center=true);
translate([-5,0,0]) cube(hlw,center=true);
translate([(le-re)/2, (be-te)/2, 5])
    cube(wnd,center=true);
}
