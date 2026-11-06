#include "../src/TuneView.hpp"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
using namespace tunebrew;
struct H : TuneView::Host { void setParam(int i,float v) override { printf("set %d=%g\n",i,v);} void beginEdit(int) override {} void endEdit(int) override {} };
// render_ui in.f32 out.png stopSec mode learn [hoverX hoverY]
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
  double stop=atof(argv[3]); int mode=atoi(argv[4]); int learn=atoi(argv[5]);
  H h; TuneView view(&h); TuneEngine e; e.init(48000,1);
  e.key=0; e.scale=1; e.retuneMs=10; e.mode=mode; e.learn=learn;
  view.setValue(pKey,0); view.setValue(pScale,1); view.setValue(pRetune,10); view.setValue(pMode,mode); view.setValue(pLearn,learn);
  std::vector<float> o(1600); size_t blk=1600;
  for(size_t i=0;i+blk<=std::min(x.size(),(size_t)(stop*48000));i+=blk){ const float* in[1]={&x[i]}; float* out[1]={o.data()}; e.process(in,out,blk);
    view.setValue(pOutPitch,e.meterHz); view.setValue(pOutNote,e.meterTarget); view.setValue(pOutCents,e.meterCents); view.setValue(pOutActivity,(i/blk)%1000);
    view.setValue(pOutVib,e.meterVib); view.setValue(pOutKey,e.meterKey); view.setValue(pOutKeyConf,e.meterKeyConf); view.setValue(pOutKeyAlt,e.meterKeyAlt);
    view.setValue(pOutTuneOff,e.meterTuneOff); view.setValue(pOutLearnSecs,e.meterLearnSecs); view.tick(i/48000.0); }
  if(argc>7) view.mouseMove(atof(argv[6]),atof(argv[7]),false);
  int W=TuneView::kW,Hh=TuneView::kH;
  cairo_surface_t* surf=cairo_image_surface_create(CAIRO_FORMAT_RGB24,W,Hh); cairo_t* cr=cairo_create(surf);
  view.draw(cr,W,Hh); cairo_surface_write_to_png(surf,argv[2]); cairo_destroy(cr); cairo_surface_destroy(surf);
  if(argc>9){ /* click test */ }
}
