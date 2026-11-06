#include "../src/TuneEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <chrono>
// usage: harness in.f32 out.f32 sr key scale retuneMs amount transpose
int main(int argc, char** argv){
  FILE* f=fopen(argv[1],"rb"); std::vector<float> x; float v; while(fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
  double sr=atof(argv[3]); tunebrew::TuneEngine e; e.init(sr,2);
  e.key=atoi(argv[4]); e.scale=atoi(argv[5]); e.retuneMs=atof(argv[6]); e.amount=atof(argv[7]); e.transpose=atof(argv[8]);
  int L=e.getLatency(); size_t n=x.size(); std::vector<float> l(n+L,0), r(n+L,0), ol(n+L), orr(n+L);
  for(size_t i=0;i<n;i++){l[i]=x[i]; r[i]=x[i];}
  auto t0=std::chrono::steady_clock::now();
  for(size_t i=0;i<n+L;i+=256){ uint32_t fr=std::min<size_t>(256,n+L-i); const float* in[2]={&l[i],&r[i]}; float* out[2]={&ol[i],&orr[i]}; e.process(in,out,fr);}
  double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  fprintf(stderr,"latency %d, realtime factor %.1fx (stereo)\n",L,(n/sr)/secs);
  f=fopen(argv[2],"wb"); fwrite(&ol[L],4,n,f); fclose(f);
  double maxd=0; for(size_t i=L;i<n+L;i++) maxd=std::max(maxd,(double)fabs(ol[i]-orr[i])); fprintf(stderr,"L/R maxdiff %g\n",maxd);
}
