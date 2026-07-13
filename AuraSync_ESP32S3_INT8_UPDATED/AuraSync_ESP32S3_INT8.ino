#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <driver/i2s_std.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "aurasync_model_data.h"
#include "aurasync_preprocessing_params.h"

// INMP441 wiring (change here if your board uses other pins)
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_5;
constexpr gpio_num_t PIN_I2S_WS   = GPIO_NUM_4;
constexpr gpio_num_t PIN_I2S_DIN  = GPIO_NUM_6;
constexpr char AP_NAME[] = "AuraSync-INT8";
constexpr char AP_PASSWORD[] = "AuraSync2026";
constexpr float MIC_DBFS_TO_SPL = 120.0f; // Calibrate against a reference meter.
constexpr float LOUD_ON_DBA = 75.0f, LOUD_OFF_DBA = 71.0f;
constexpr float SPIKE_CREST_DB = 14.0f, SPIKE_RISE_DB = 8.0f;

WebServer server(80);
i2s_chan_handle_t rxChannel = nullptr;
int16_t audio[AURASYNC_TARGET_SAMPLES];
float fftRe[AURASYNC_N_FFT], fftIm[AURASYNC_N_FFT];
float melFeatures[AURASYNC_TIME_FRAMES * AURASYNC_N_MELS];
float probabilities[3] = {1,0,0};
float laeq = 0, peakDb = 0, baselineDb = 45, score = 0;
bool spike = false, loudLatched = false, modelReady = false, micReady = false;
uint32_t inferenceMs = 0, sequenceNo = 0;
String stateName = "BOOT";

constexpr size_t TENSOR_ARENA_BYTES = 420 * 1024;
uint8_t *tensorArena = nullptr;
const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *inputTensor = nullptr, *outputTensor = nullptr;

static float hzToMel(float hz) {
  // librosa/Slaney mel scale
  constexpr float f_sp = 200.0f / 3.0f;
  float m = hz / f_sp;
  if (hz >= 1000.0f) m = 15.0f + logf(hz / 1000.0f) / (logf(6.4f) / 27.0f);
  return m;
}
static float melToHz(float m) {
  constexpr float f_sp = 200.0f / 3.0f;
  if (m < 15.0f) return f_sp * m;
  return 1000.0f * expf((logf(6.4f) / 27.0f) * (m - 15.0f));
}

static void fft512(float *re, float *im) {
  const int n=AURASYNC_N_FFT;
  for (int i=1,j=0;i<n;i++) { int bit=n>>1; for(;j&bit;bit>>=1)j^=bit; j^=bit; if(i<j){std::swap(re[i],re[j]);std::swap(im[i],im[j]);} }
  for(int len=2;len<=n;len<<=1){
    float a=-2.0f*PI/len, wr0=cosf(a), wi0=sinf(a);
    for(int i=0;i<n;i+=len){ float wr=1,wi=0; for(int j=0;j<len/2;j++){
      int u=i+j,v=u+len/2; float vr=re[v]*wr-im[v]*wi,vi=re[v]*wi+im[v]*wr;
      re[v]=re[u]-vr; im[v]=im[u]-vi; re[u]+=vr; im[u]+=vi;
      float nw=wr*wr0-wi*wi0; wi=wr*wi0+wi*wr0; wr=nw;
    }}
  }
}

static bool captureOneSecond() {
  size_t got=0,total=0; int32_t raw[256];
  while(total<AURASYNC_TARGET_SAMPLES){
    size_t want=min((size_t)256,(size_t)AURASYNC_TARGET_SAMPLES-total);
    if(i2s_channel_read(rxChannel,raw,want*sizeof(int32_t),&got,1000)!=ESP_OK) return false;
    // INMP441 data is 24-bit, left-aligned in the 32-bit I2S word.
    int count=got/4; for(int i=0;i<count;i++) audio[total++]=(int16_t)(raw[i]>>16);
  }
  return true;
}

static void calculateDsp() {
  double sum=0; int32_t pk=1;
  for(int i=0;i<AURASYNC_TARGET_SAMPLES;i++){ float x=audio[i]/32768.0f; sum+=x*x; pk=max(pk,abs((int32_t)audio[i])); }
  float rms=sqrtf(sum/AURASYNC_TARGET_SAMPLES);
  laeq=20*log10f(max(rms,1e-9f))+MIC_DBFS_TO_SPL;
  peakDb=20*log10f(pk/32768.0f)+MIC_DBFS_TO_SPL;
  // A spike must be both impulsive (high crest) and above the adaptive floor.
  // This prevents ordinary steady audio from leaving the SPIKE indicator on.
  float crest=peakDb-laeq, rise=laeq-baselineDb;
  spike=(crest>=SPIKE_CREST_DB && rise>=SPIKE_RISE_DB);
  if(laeq<baselineDb+8) baselineDb=0.995f*baselineDb+0.005f*laeq;
  if(!loudLatched && laeq>=LOUD_ON_DBA) loudLatched=true;
  else if(loudLatched && laeq<=LOUD_OFF_DBA) loudLatched=false;
}

static void extractLogMelInt8() {
  double dc=0; for(int i=0;i<AURASYNC_TARGET_SAMPLES;i++) dc+=audio[i]; dc/=AURASYNC_TARGET_SAMPLES;
  const float melMin=hzToMel(AURASYNC_FMIN), melMax=hzToMel(AURASYNC_FMAX);
  float edges[AURASYNC_N_MELS+2];
  for(int m=0;m<AURASYNC_N_MELS+2;m++) edges[m]=melToHz(melMin+(melMax-melMin)*m/(AURASYNC_N_MELS+1));
  double clipMean=0;
  for(int t=0;t<AURASYNC_TIME_FRAMES;t++){
    int start=t*AURASYNC_HOP_LENGTH;
    for(int i=0;i<AURASYNC_N_FFT;i++){
      float hann=0.5f-0.5f*cosf(2.0f*PI*i/AURASYNC_WIN_LENGTH); // periodic Hann
      fftRe[i]=((audio[start+i]-dc)/32768.0f)*hann; fftIm[i]=0;
    }
    fft512(fftRe,fftIm);
    for(int m=0;m<AURASYNC_N_MELS;m++){
      double e=0; float enorm=2.0f/(edges[m+2]-edges[m]);
      for(int k=0;k<=AURASYNC_N_FFT/2;k++){
        float hz=(float)k*AURASYNC_SAMPLE_RATE/AURASYNC_N_FFT,w=0;
        if(hz>=edges[m]&&hz<edges[m+1]) w=(hz-edges[m])/(edges[m+1]-edges[m]);
        else if(hz>=edges[m+1]&&hz<=edges[m+2]) w=(edges[m+2]-hz)/(edges[m+2]-edges[m+1]);
        if(w>0) e+=(fftRe[k]*fftRe[k]+fftIm[k]*fftIm[k])*w*enorm;
      }
      float v=logf(max((float)e,AURASYNC_LOG_EPS)); melFeatures[t*AURASYNC_N_MELS+m]=v; clipMean+=v;
    }
  }
  clipMean/=AURASYNC_TIME_FRAMES*AURASYNC_N_MELS;
  for(int t=0;t<AURASYNC_TIME_FRAMES;t++) for(int m=0;m<AURASYNC_N_MELS;m++){
    float z=(melFeatures[t*AURASYNC_N_MELS+m]-clipMean-AURASYNC_MEL_MEAN[m])/AURASYNC_MEL_STD[m];
    int q=lrintf(z/AURASYNC_INPUT_SCALE)+AURASYNC_INPUT_ZERO_POINT;
    inputTensor->data.int8[t*AURASYNC_N_MELS+m]=(int8_t)constrain(q,-128,127);
  }
}

static bool runModel() {
  if(!modelReady) return false; uint32_t t=millis(); extractLogMelInt8();
  if(interpreter->Invoke()!=kTfLiteOk) return false; inferenceMs=millis()-t;
  float sum=0; for(int i=0;i<3;i++){ probabilities[i]=(outputTensor->data.int8[i]-AURASYNC_OUTPUT_ZERO_POINT)*AURASYNC_OUTPUT_SCALE; probabilities[i]=max(0.0f,probabilities[i]);sum+=probabilities[i]; }
  if(sum>0) for(float &p:probabilities)p/=sum; return true;
}

static void fuseState() {
  float loudSeverity=constrain((laeq-45.0f)/40.0f,0.0f,1.0f);
  float semantic=probabilities[1]+0.55f*probabilities[2];
  score=100.0f*constrain(0.45f*loudSeverity+0.35f*semantic+0.20f*(spike?1.0f:0.0f),0.0f,1.0f);
  float conf=max(probabilities[0],max(probabilities[1],probabilities[2]));
  if(!micReady) stateName="MIC_ERROR"; else if(spike) stateName="SPIKE"; else if(loudLatched) stateName="LOUD";
  else if(conf<AURASYNC_LIVE_CONFIDENCE_THRESHOLD) stateName="UNCERTAIN";
  else if(score>=60) stateName="WARNING"; else if(score>=35) stateName="PRE_WARNING"; else stateName="SMOOTH";
}

static String jsonStatus(){
  String s="{\"sequence\":"+String(sequenceNo)+",\"state\":\""+stateName+"\",\"score\":"+String(score,1)+",\"laeq\":"+String(laeq,1)+",\"peak\":"+String(peakDb,1)+",\"baseline\":"+String(baselineDb,1)+",\"spike\":"+(spike?"true":"false")+",\"model_ready\":"+(modelReady?"true":"false")+",\"inference_ms\":"+String(inferenceMs)+",\"p\":["+String(probabilities[0],3)+","+String(probabilities[1],3)+","+String(probabilities[2],3)+"]}"; return s;
}

const char PAGE[] PROGMEM=R"HTML(<!doctype html><meta name=viewport content="width=device-width,initial-scale=1"><title>AuraSync</title><style>body{font-family:system-ui;background:#07111f;color:#e8f0ff;margin:0}.w{max-width:900px;margin:auto;padding:24px}.hero,.card{background:#102238;border:1px solid #244565;border-radius:18px;padding:20px;margin:12px 0}.hero{text-align:center}.state{font-size:42px;font-weight:800}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.v{font-size:30px;font-weight:700;color:#5eead4}.bar{height:13px;background:#263b50;border-radius:9px;overflow:hidden}.bar i{display:block;height:100%;background:#38bdf8}.small{color:#9fb3c8}</style><div class=w><h2>AuraSync ESP32-S3</h2><div class=hero><div class=small>ACOUSTIC STATE</div><div id=st class=state>CONNECTING</div><div id=sc class=v>--</div><div>Disturbance score / 100</div></div><div class=grid><div class=card><div class=small>Estimated calibrated LAeq</div><div id=db class=v>--</div></div><div class=card><div class=small>Peak</div><div id=pk class=v>--</div></div><div class=card><div class=small>Inference</div><div id=ms class=v>--</div></div></div><div class=card><h3>CNN probabilities</h3><div>Stationary background <b id=p0></b><div class=bar><i id=b0></i></div></div><div>Speech dominant <b id=p1></b><div class=bar><i id=b1></i></div></div><div>Non-speech activity <b id=p2></b><div class=bar><i id=b2></i></div></div></div><p class=small>Model v2.0.0 · INT8 DS-CNN · LOUD/SPIKE are DSP outputs · No audio leaves this device.</p></div><script>async function u(){try{let d=await(await fetch('/api/status')).json();st.textContent=d.state;sc.textContent=d.score.toFixed(1);db.textContent=d.laeq.toFixed(1)+' dBA*';pk.textContent=d.peak.toFixed(1)+' dB';ms.textContent=d.inference_ms+' ms';d.p.forEach((x,i)=>{document.querySelector('#p'+i).textContent=(x*100).toFixed(1)+'%';document.querySelector('#b'+i).style.width=(x*100)+'%'})}catch(e){}setTimeout(u,1000)}u()</script>)HTML";

static bool initMic(){
  i2s_chan_config_t c=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER); if(i2s_new_channel(&c,nullptr,&rxChannel)!=ESP_OK)return false;
  i2s_std_config_t s={}; s.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(AURASYNC_SAMPLE_RATE); s.slot_cfg=I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_MONO); s.slot_cfg.slot_mask=I2S_STD_SLOT_LEFT; s.gpio_cfg.mclk=I2S_GPIO_UNUSED;s.gpio_cfg.bclk=PIN_I2S_BCLK;s.gpio_cfg.ws=PIN_I2S_WS;s.gpio_cfg.dout=I2S_GPIO_UNUSED;s.gpio_cfg.din=PIN_I2S_DIN;s.gpio_cfg.invert_flags={false,false,false};
  return i2s_channel_init_std_mode(rxChannel,&s)==ESP_OK&&i2s_channel_enable(rxChannel)==ESP_OK;
}
static bool initModel(){
  model=tflite::GetModel(g_aurasync_3class_model); if(model->version()!=TFLITE_SCHEMA_VERSION)return false;
  tensorArena=(uint8_t*)heap_caps_malloc(TENSOR_ARENA_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); if(!tensorArena)tensorArena=(uint8_t*)heap_caps_malloc(TENSOR_ARENA_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if(!tensorArena)return false;
  static tflite::MicroMutableOpResolver<10> resolver; resolver.AddConv2D();resolver.AddDepthwiseConv2D();resolver.AddFullyConnected();resolver.AddMean();resolver.AddReshape();resolver.AddSoftmax();resolver.AddAdd();resolver.AddMul();resolver.AddQuantize();resolver.AddDequantize();
  static tflite::MicroInterpreter staticInterpreter(model,resolver,tensorArena,TENSOR_ARENA_BYTES); interpreter=&staticInterpreter;
  if(interpreter->AllocateTensors()!=kTfLiteOk)return false; inputTensor=interpreter->input(0);outputTensor=interpreter->output(0);
  return inputTensor->type==kTfLiteInt8&&inputTensor->bytes==AURASYNC_TIME_FRAMES*AURASYNC_N_MELS&&outputTensor->type==kTfLiteInt8&&outputTensor->bytes==3;
}
void setup(){ Serial.begin(115200);delay(500);micReady=initMic();modelReady=initModel();WiFi.mode(WIFI_AP);WiFi.softAP(AP_NAME,AP_PASSWORD);server.on("/",[]{server.send_P(200,"text/html",PAGE);});server.on("/api/status",[]{server.send(200,"application/json",jsonStatus());});server.onNotFound([]{server.send(404,"text/plain","Not found");});server.begin();Serial.printf("Dashboard: http://%s\n",WiFi.softAPIP().toString().c_str()); }
void loop(){ server.handleClient();static uint32_t last=0;if(millis()-last<1000){delay(2);return;}last=millis();if(micReady&&captureOneSecond()){calculateDsp();runModel();sequenceNo++;}else micReady=false;fuseState();server.handleClient(); }
