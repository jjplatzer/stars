#include "frame.h"
#include "builders.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace stars::renderer;
void check(bool value) { if (!value) throw std::runtime_error("frame replay contract failed"); }
void close(float a,float b) { check(std::isfinite(a) && std::abs(a-b)<0.00001f); }
void triangle(CmdBuffer& cb) {
    TrianglesBuilder b; b.addTriangle({0,0},{100,0},{0,100}); b.generateCommands(cb);
}
int main() {
    try {
        ZCmdBuffer z;
        auto child=std::make_shared<CmdBuffer>(); child->setRGB({1,0,0}); triangle(*child);
        auto& low=z.at(-1); low.loadProjectionMatrix(screenOrtho(100,100)); low.call(child);
        auto& high=z.at(5); triangle(high);
        auto f=prepare(z,100,100);
        check(f.batches.size()==2 && f.vertices.size()==6);
        check(f.stats.buffers==3 && f.stats.drawCalls==2 && f.stats.triangles==2 && f.stats.lines==0);
        check(f.stats.bufferBytes==2*(3*sizeof(PointVertex)+3*sizeof(std::uint32_t)));
        check(f.stats.gpuDrawCalls==2 && f.stats.uploadBytes==6*sizeof(Vertex));
        Stats accumulated; accumulated+=f.stats; accumulated+=f.stats;
        check(accumulated.buffers==6 && accumulated.triangles==4);
        close(f.vertices[0].x,-1); close(f.vertices[0].y,1); close(f.vertices[0].z,0.5);
        close(f.vertices[3].color.r,1); close(f.vertices[3].y,1); // child state survives call and Z transition
        CmdBuffer cb;
        cb.viewport({20,10,50,40}); cb.scissor({-10,90,30,30}); cb.clear({0,1,0,1});
        cb.resetState(); triangle(cb);
        f=prepare(cb,100,100);
        check(f.batches.size()==2);
        const auto& clear=f.batches[0]; const auto& draw=f.batches[1];
        check(!clear.blend && clear.viewport.w==100 && clear.viewport.h==100);
        check(clear.scissor.x==0 && clear.scissor.y==0 && clear.scissor.w==20 && clear.scissor.h==10);
        check(draw.blend && draw.viewport.x==20 && draw.viewport.y==50 && draw.viewport.w==50);
        check(draw.scissor.w==100 && draw.scissor.h==100);
        close(f.vertices[6].color.r,0); close(f.vertices[6].color.a,1);
        cb.reset(); cb.loadProjectionMatrix(screenOrtho(100,100)); cb.lineWidth(4);
        LinesBuilder lines; lines.addLine({-10,50},{110,50}); lines.addLine({1,1},{1,1}); lines.generateCommands(cb);
        f=prepare(cb,100,100); check(f.vertices.size()==6);
        check(f.stats.lines==2 && f.stats.triangles==0 && f.stats.gpuDrawCalls==1);
        float minX=1,maxX=-1,minY=1,maxY=-1;
        for(auto v:f.vertices) { minX=std::min(minX,v.x); maxX=std::max(maxX,v.x); minY=std::min(minY,v.y); maxY=std::max(maxY,v.y); }
        close(minX,-1); close(maxX,1); close(minY,-0.04); close(maxY,0.04);
        cb.reset(); cb.scissor({100,100,0,0}); triangle(cb); cb.clear({1,1,1,1});
        check(prepare(cb,100,100).batches.empty());
        bool rejected=false; try { prepare(cb,0,100); } catch(const std::invalid_argument&) { rejected=true; } check(rejected);
        // Assignment can form a cycle after call() validation: replay must still reject it.
        auto a=std::make_shared<CmdBuffer>(); a->setRGB({1,1,1});
        auto b=std::make_shared<CmdBuffer>(); b->call(a);
        *a=*b;
        rejected=false; try { prepare(*a,100,100); } catch(const std::invalid_argument&) { rejected=true; }
        a->reset(); check(rejected);
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
