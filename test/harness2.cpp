// harness2 in.f32 out.f32 [name=value ...]   (48 kHz mono float in/out, latency compensated)
#include "../src/TuneEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
int main(int argc, char** argv){
  FILE* f=fopen(argv[1],"rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
  double sr=48000; tunebrew::TuneEngine e; e.init(sr,1);
  for(int i=3;i<argc;i++){ char k[64]; double val; if(sscanf(argv[i],"%63[^=]=%lf",k,&val)!=2) continue; std::string s(k);
    if(s=="key")e.key=(int)val; else if(s=="scale")e.scale=(int)val; else if(s=="retune")e.retuneMs=val; else if(s=="amount")e.amount=val;
    else if(s=="transpose")e.transpose=val; else if(s=="ref")e.refHz=val; else if(s=="mode")e.mode=(int)val; else if(s=="vibrato")e.vibrato=val;
    else if(s=="flex")e.flex=val; else if(s=="humanize")e.humanize=val; else if(s=="transition")e.transitionMs=val; else if(s=="learn")e.learn=val>0;
    else if(s=="sr"){sr=val; e.init(sr,1);} }
  int L=e.getLatency(); size_t n=x.size(); x.resize(n+L,0); std::vector<float> o(n+L); int vibHops=0;
  for(size_t i=0;i<n+L;i+=256){ uint32_t fr=std::min<size_t>(256,n+L-i); const float* in[1]={&x[i]}; float* out[1]={&o[i]}; e.process(in,out,fr); if(e.meterVib>0) vibHops++; }
  f=fopen(argv[2],"wb"); fwrite(&o[L],4,n,f); fclose(f);
  const char* kn[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int k=(int)e.meterKey, a=(int)e.meterKeyAlt;
  printf("vibblocks=%d key=%s%s conf=%.2f alt=%s%s offset=%.1f secs=%.1f\n", vibHops,
     k>=0?kn[k%12]:"-", k>=12?"m":"", e.meterKeyConf, a>=0?kn[a%12]:"-", a>=12?"m":"", e.meterTuneOff, e.meterLearnSecs);
}
