#include "tess.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace stars::renderer;
void check(bool ok) { if(!ok) throw std::runtime_error("polygon tessellation failed"); }
double area(const TrianglesBuilder& mesh) {
    check(mesh.indices.size()%3==0);
    double sum=0;
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        auto a=mesh.vertices.at(mesh.indices[i]),b=mesh.vertices.at(mesh.indices[i+1]),c=mesh.vertices.at(mesh.indices[i+2]);
        sum+=std::abs(double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x))/2;
    }
    return sum;
}
int main() {
    try {
        std::vector<std::vector<PointVertex>> rings={{{0,0},{10,0},{10,10},{0,10}},{{2,2},{2,8},{8,8},{8,2}}};
        for(int winding=0;winding<4;++winding) {
            auto mesh=tessellate(rings); check(area(mesh)==64);
            // No triangle may place its centroid inside the hole.
            for(std::size_t i=0;i<mesh.indices.size();i+=3) {
                PointVertex p{};
                for(unsigned j=0;j<3;++j) { auto v=mesh.vertices[mesh.indices[i+j]]; p.x+=v.x/3; p.y+=v.y/3; }
                check(!(p.x>2 && p.x<8 && p.y>2 && p.y<8));
            }
            std::reverse(rings[winding%2].begin(),rings[winding%2].end());
        }
        rings={{{0,0},{10,0},{10,4},{4,4},{4,10},{0,10},{0,0}}};
        check(area(tessellate(rings))==64); // concave and explicitly closed
        rings={{{0,0},{20,0},{20,20},{0,20}},{{2,2},{4,2},{4,4},{2,4}},{{10,10},{15,10},{15,15},{10,15}}};
        check(area(tessellate(rings))==371); // two holes
        rings={{{1,1}},{{0,0},{1,0},{2,0}}}; check(tessellate(rings).indices.empty());
        rings.clear(); check(tessellate(rings).vertices.empty());
        rings={{{0,0},{1,0},{0,std::numeric_limits<float>::infinity()}}};
        bool failed=false; try { tessellate(rings); } catch(const std::invalid_argument&) { failed=true; } check(failed);
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
