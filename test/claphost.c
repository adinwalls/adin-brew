#include "entry.h"
#include "plugin-factory.h"
#include "ext/latency.h"
#include <stdbool.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static const void* get_ext(const clap_host_t* h,const char* id){return NULL;}
static void noop(const clap_host_t* h){}
static uint32_t sz(const clap_input_events_t* l){return 0;}
static const clap_event_header_t* gt(const clap_input_events_t* l,uint32_t i){return NULL;}
static bool tp(const clap_output_events_t* l,const clap_event_header_t* ev){return true;}
int main(int argc,char**argv){ setvbuf(stdout,NULL,_IONBF,0);
  void* lib=dlopen(argv[1],RTLD_NOW); if(!lib){printf("dlopen: %s\n",dlerror());return 1;}
  const clap_plugin_entry_t* e=dlsym(lib,"clap_entry"); e->init(argv[1]);
  const clap_plugin_factory_t* f=e->get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_descriptor_t* d=f->get_plugin_descriptor(f,0); printf("id %s name %s\n",d->id,d->name);
  clap_host_t host={CLAP_VERSION,NULL,"test","t","","1",get_ext,noop,noop,noop};
  const clap_plugin_t* p=f->create_plugin(f,&host,d->id); p->init(p);
  const clap_plugin_latency_t* lat=p->get_extension(p,CLAP_EXT_LATENCY);
  if(!p->activate(p,48000,32,512)){printf("activate failed\n");return 1;} printf("latency %u\n",lat?lat->get(p):0);
  p->start_processing(p);
  static float inL[512],inR[512],oL[512],oR[512]; float* ins[2]={inL,inR}; float* outs[2]={oL,oR};
  clap_audio_buffer_t ib={ins,NULL,2,0,0},ob={outs,NULL,2,0,0};
  clap_input_events_t iev={NULL,(void*)0,(void*)0};
  iev.size=sz; iev.get=gt; clap_output_events_t oev={NULL,tp};
  double ph=0, energy=0;
  for(int b=0;b<400;b++){ for(int i=0;i<512;i++){ph+=2*M_PI*233.0/48000; inL[i]=inR[i]=0.3*(sin(ph)+0.5*sin(2*ph)+0.3*sin(3*ph));}
    clap_process_t pr={0}; pr.frames_count=512; pr.audio_inputs=&ib; pr.audio_outputs=&ob; pr.audio_inputs_count=1; pr.audio_outputs_count=1; pr.in_events=&iev; pr.out_events=&oev; pr.steady_time=b*512;
    p->process(p,&pr); if(b>300) for(int i=0;i<512;i++) energy+=oL[i]*oL[i]; }
  printf("output rms %.3f\n",sqrt(energy/(99*512)));
  p->stop_processing(p); p->deactivate(p); p->destroy(p); e->deinit(); return 0;}
