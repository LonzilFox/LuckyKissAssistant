#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "Shared.h"

// Unity IL2CPP single-player prototype. All Unity gameplay methods run in the
// game's own callbacks. Only primitive fields/save-data methods are attempted
// on the attached worker thread, on explicit user requests.
namespace {
using IlObj=void;using IlClass=void;using IlImage=void;using IlDomain=void;
using IlAssembly=void;using IlMethod=void;using IlField=void;using IlType=void;using IlThread=void;
struct Api {
 IlDomain* (*domain_get)()=nullptr;
 const IlAssembly** (*domain_get_assemblies)(IlDomain*,size_t*)=nullptr;
 const IlImage* (*assembly_get_image)(const IlAssembly*)=nullptr;
 const char* (*image_get_name)(const IlImage*)=nullptr;
 IlClass* (*class_from_name)(const IlImage*,const char*,const char*)=nullptr;
 IlClass* (*object_get_class)(IlObj*)=nullptr;
 const char* (*class_get_name)(IlClass*)=nullptr;
 const IlMethod* (*class_get_method_from_name)(IlClass*,const char*,int)=nullptr;
 IlField* (*class_get_field_from_name)(IlClass*,const char*)=nullptr;
 const IlType* (*field_get_type)(IlField*)=nullptr;
 unsigned int (*field_get_flags)(IlField*)=nullptr;
 int (*type_get_type)(const IlType*)=nullptr;
 const IlType* (*method_get_return_type)(const IlMethod*)=nullptr;
 unsigned int (*method_get_param_count)(const IlMethod*)=nullptr;
 const IlType* (*method_get_param)(const IlMethod*,unsigned int)=nullptr;
 void (*field_get_value)(IlObj*,IlField*,void*)=nullptr;
 void (*field_set_value)(IlObj*,IlField*,void*)=nullptr;
 void (*field_static_get_value)(IlField*,void*)=nullptr;
 IlObj* (*runtime_invoke)(const IlMethod*,IlObj*,void**,IlObj**)=nullptr;
 void* (*object_unbox)(IlObj*)=nullptr;
 IlThread* (*thread_attach)(IlDomain*)=nullptr;
 void (*thread_detach)(IlThread*)=nullptr;
 unsigned int (*method_get_flags)(const IlMethod*,unsigned int*)=nullptr;
} api;
SharedState* state=nullptr;
HANDLE mapping=nullptr;
IlField *bankField=nullptr,*selectedField=nullptr,*activeField=nullptr,*finalField=nullptr,*topField=nullptr,*stoppedField=nullptr,*spawnedField=nullptr;
const IlMethod *forceDice=nullptr,*saveBank=nullptr,*readBankFromData=nullptr,*refreshTexts=nullptr;
volatile LONG goldLock=0;
volatile LONG goldRefreshSequence=0;
HANDLE workEvent=nullptr;
IlClass* dataClass=nullptr;
const IlMethod* gameDataGetter=nullptr;
int bankType=0;
thread_local bool inDice=false;
constexpr int TYPE_VOID=1,TYPE_I4=8,TYPE_U4=9,TYPE_I8=10,TYPE_U8=11,TYPE_R4=12,TYPE_R8=13;

void Status(const char* t){if(state)strncpy_s(state->status,sizeof(state->status),t,_TRUNCATE);}
template<typename T> bool Import(HMODULE mod,T& p,const char* name){p=reinterpret_cast<T>(GetProcAddress(mod,name));return p!=nullptr;}
bool InitApi(HMODULE m){
 bool ok=Import(m,api.domain_get,"il2cpp_domain_get")&&
 Import(m,api.domain_get_assemblies,"il2cpp_domain_get_assemblies")&&
 Import(m,api.assembly_get_image,"il2cpp_assembly_get_image")&&
 Import(m,api.image_get_name,"il2cpp_image_get_name")&&
 Import(m,api.class_from_name,"il2cpp_class_from_name")&&
 Import(m,api.object_get_class,"il2cpp_object_get_class")&&
 Import(m,api.class_get_name,"il2cpp_class_get_name")&&
 Import(m,api.class_get_method_from_name,"il2cpp_class_get_method_from_name")&&
 Import(m,api.class_get_field_from_name,"il2cpp_class_get_field_from_name")&&
 Import(m,api.field_get_type,"il2cpp_field_get_type")&&
 Import(m,api.field_get_flags,"il2cpp_field_get_flags")&&
 Import(m,api.type_get_type,"il2cpp_type_get_type")&&
 Import(m,api.method_get_return_type,"il2cpp_method_get_return_type")&&
 Import(m,api.method_get_param_count,"il2cpp_method_get_param_count")&&
 Import(m,api.method_get_param,"il2cpp_method_get_param")&&
 Import(m,api.field_get_value,"il2cpp_field_get_value")&&
 Import(m,api.field_set_value,"il2cpp_field_set_value")&&
 Import(m,api.field_static_get_value,"il2cpp_field_static_get_value")&&
 Import(m,api.runtime_invoke,"il2cpp_runtime_invoke")&&
 Import(m,api.object_unbox,"il2cpp_object_unbox")&&
 Import(m,api.thread_attach,"il2cpp_thread_attach")&&
 Import(m,api.thread_detach,"il2cpp_thread_detach");
 Import(m,api.method_get_flags,"il2cpp_method_get_flags");
 return ok;
}
IlClass* FindClass(const char* name){
 size_t n=0;auto d=api.domain_get();if(!d)return nullptr;
 auto aa=api.domain_get_assemblies(d,&n);if(!aa||n>2000)return nullptr;
 for(size_t i=0;i<n;++i){auto image=api.assembly_get_image(aa[i]);if(!image)continue;
  auto imageName=api.image_get_name(image);
  if(imageName&&strstr(imageName,"Assembly-CSharp")){
   if(auto result=api.class_from_name(image,"",name))return result;
  }
 }
 return nullptr;
}
bool IsReadable(const void* ptr,size_t len){
 if(!ptr||len==0)return false;
 MEMORY_BASIC_INFORMATION mb{};
 if(!VirtualQuery(ptr,&mb,sizeof mb)||mb.State!=MEM_COMMIT)return false;
 DWORD p=mb.Protect&0xff;
 if(p==PAGE_NOACCESS||p==PAGE_EXECUTE||mb.Protect&PAGE_GUARD)return false;
 uintptr_t end=reinterpret_cast<uintptr_t>(ptr)+len;
 return end>=reinterpret_cast<uintptr_t>(ptr)&&end<=reinterpret_cast<uintptr_t>(mb.BaseAddress)+mb.RegionSize;
}
bool Executable(const void* p){
 MEMORY_BASIC_INFORMATION mb{};if(!p||!VirtualQuery(p,&mb,sizeof mb))return false;
 DWORD prot=mb.Protect&0xff;
 return mb.State==MEM_COMMIT && (prot==PAGE_EXECUTE||prot==PAGE_EXECUTE_READ||prot==PAGE_EXECUTE_READWRITE||prot==PAGE_EXECUTE_WRITECOPY);
}
void* Native(const IlMethod* m){if(!m||!IsReadable(m,sizeof(void*)))return nullptr;auto ptr=*reinterpret_cast<void* const*>(m);return Executable(ptr)?ptr:nullptr;}
// Conservative x64 prologue decoder for a 5-byte patch. Rejects RIP-relative
// memory, relative control flow, and unknown opcodes. Never blindly slices an
// instruction, and always preserves the original code through a trampoline.
size_t SafeLength(const unsigned char* code){
 if(!IsReadable(code,16))return 0;
 size_t i=0;bool op66=false,rexW=false;
 while(i<8&&(code[i]==0x40||(code[i]>=0x41&&code[i]<=0x4f)||code[i]==0x66)){
  if(code[i]==0x66)op66=true;
  if((code[i]&0xf8)==0x48)rexW=true;
  ++i;
 }
 if(i>=8)return 0;
 unsigned char op=code[i++];
 if((op>=0x50&&op<=0x5f)||op==0x90)return i;
 if(op==0xC3||op==0xC2||op==0xCC||op==0xE8||op==0xE9||op==0xEB||
    (op>=0x70&&op<=0x7f)||op==0x0f)return 0;
 if(op==0x68){i+=op66?2:4;return i<=15?i:0;}
 if(op==0x6a){++i;return i;}
 if(op>=0xB8&&op<=0xBF){i+=rexW?8:op66?2:4;return i<=15?i:0;}
 // Common register/memory operations with ModR/M.
 bool hasModrm=op==0x8B||op==0x89||op==0x8D||op==0x85||op==0x31||op==0x33||
  op==0x3B||op==0x39||op==0x8A||op==0x88||op==0x83||op==0x81||op==0x80||
  op==0xC7||op==0xC6||op==0xFF;
 if(!hasModrm||i>=15)return 0;
 unsigned char m=code[i++];unsigned mod=m>>6,rm=m&7;
 if(mod!=3&&rm==4){ // SIB
  unsigned char sib=code[i++];
  if(mod==0&&(sib&7)==5)i+=4; // absolute base, not RIP-relative
 } else if(mod==0&&rm==5)return 0; // RIP-relative instruction is unsupported
 if(mod==1)i+=1;
 else if(mod==2)i+=4;
 if(op==0x83||op==0x80||op==0xC6)i+=1;
 else if(op==0x81||op==0xC7)i+=op66?2:4;
 return i<=15?i:0;
}
void* NearBlock(void* origin){
 SYSTEM_INFO info{};GetSystemInfo(&info);
 uintptr_t gran=info.dwAllocationGranularity,base=reinterpret_cast<uintptr_t>(origin)&~(gran-1);
 for(uintptr_t delta=gran;delta<0x7ff00000ULL;delta+=gran){
  for(int sign=0;sign<2;++sign){
   if(sign&&base<delta)continue;
   uintptr_t candidate=sign?base-delta:base+delta;
   auto diff=static_cast<int64_t>(candidate)-static_cast<int64_t>(reinterpret_cast<uintptr_t>(origin));
   if(diff<INT32_MIN||diff>INT32_MAX)continue;
   MEMORY_BASIC_INFORMATION mb{};
   if(!VirtualQuery(reinterpret_cast<void*>(candidate),&mb,sizeof mb)||mb.State!=MEM_FREE||
      candidate+gran< candidate || reinterpret_cast<uintptr_t>(mb.BaseAddress)+mb.RegionSize<candidate+gran)continue;
   if(void* p=VirtualAlloc(reinterpret_cast<void*>(candidate),gran,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE))return p;
  }
 }
 return nullptr;
}
void AbsJump(unsigned char* dest,const void* target){
 // jmp [rip+0], followed by 8-byte absolute address; no register clobber.
 dest[0]=0xFF;dest[1]=0x25;std::memset(dest+2,0,4);
 auto value=static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(target));std::memcpy(dest+6,&value,8);
}
bool Install(const IlMethod* method,void* replacement,void** original){
 auto* code=static_cast<unsigned char*>(Native(method));
 if(!code||!replacement||!original)return false;
 size_t copy=0;
 while(copy<5){size_t part=SafeLength(code+copy);if(!part||copy+part>32)return false;copy+=part;}
 void* pool=NearBlock(code);if(!pool)return false;
 auto* buf=static_cast<unsigned char*>(pool);
 std::memcpy(buf,code,copy);
 AbsJump(buf+copy,code+copy);
 AbsJump(buf+128,replacement);
 auto diff=static_cast<int64_t>(reinterpret_cast<uintptr_t>(buf+128))-
           static_cast<int64_t>(reinterpret_cast<uintptr_t>(code)+5);
 if(diff<INT32_MIN||diff>INT32_MAX){VirtualFree(pool,0,MEM_RELEASE);return false;}
 unsigned char patch[5]={0xe9};auto rel=static_cast<int32_t>(diff);std::memcpy(patch+1,&rel,4);
 DWORD old=0;
 if(!VirtualProtect(code,copy,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(pool,0,MEM_RELEASE);return false;}
 std::memcpy(code,patch,5);if(copy>5)std::memset(code+5,0x90,copy-5);
 FlushInstructionCache(GetCurrentProcess(),code,copy);
 FlushInstructionCache(GetCurrentProcess(),buf,142);
 DWORD ignored=0;VirtualProtect(code,copy,old,&ignored);
 *original=buf;
 return true;
}
int FieldType(IlField* f){return f?api.type_get_type(api.field_get_type(f)):0;}
int ReadI4(IlObj* obj,IlField* f){int value=0;if(obj&&f)api.field_get_value(obj,f,&value);return value;}
IlObj* ActiveController(){
 if(!activeField||(api.field_get_flags(activeField)&0x10)==0)return nullptr;
 IlObj* controller=nullptr;api.field_static_get_value(activeField,&controller);
 if(controller&&state)InterlockedExchange(&state->controllerObserved,1);
 return controller;
}
void ObserveSelection(int face,int source){
 if(!state||face<1||face>6)return;
 InterlockedExchange(&state->gameFace,face);
 InterlockedExchange(&state->gameFaceSource,source);
 if(source==1||source==2)InterlockedIncrement(&state->selectedReads);
}
int GetFace(){
 if(!state)return 0;
 int mode=state->diceMode;
 int face=mode==2?state->desiredFace:mode==1?state->gameFace:0;
 if(mode==1){ // Read the scene's own field only at the real result event.
  if(auto controller=ActiveController()){
   int actual=ReadI4(controller,selectedField);
   if(actual>=1&&actual<=6){face=actual;ObserveSelection(actual,3);}
  }
 }
 return face>=1&&face<=6?face:0;
}
int ReadBank(IlObj* c){
 if(!c||!bankField)return -1;
 switch(bankType){
  case TYPE_I4:case TYPE_U4:{int v=0;api.field_get_value(c,bankField,&v);return v;}
  case TYPE_R4:{float v=0;api.field_get_value(c,bankField,&v);return static_cast<int>(v);}
  case TYPE_R8:{double v=0;api.field_get_value(c,bankField,&v);return static_cast<int>(v);}
  case TYPE_I8:case TYPE_U8:{int64_t v=0;api.field_get_value(c,bankField,&v);return static_cast<int>(v);}
 }
 return -1;
}
bool WriteBank(IlObj* c,int value){
 if(!c||!bankField)return false;
 switch(bankType){
  case TYPE_I4:case TYPE_U4:{int v=value;api.field_set_value(c,bankField,&v);break;}
  case TYPE_R4:{float v=static_cast<float>(value);api.field_set_value(c,bankField,&v);break;}
  case TYPE_R8:{double v=static_cast<double>(value);api.field_set_value(c,bankField,&v);break;}
  case TYPE_I8:case TYPE_U8:{int64_t v=value;api.field_set_value(c,bankField,&v);break;}
  default:return false;
 }
 InterlockedIncrement(&state->bankWrites);return true;
}
bool InvokeBankSave(IlObj* controller,int desired){
 if(!saveBank){InterlockedExchange(&state->goldDataStatus,0);return false;}
 auto n=api.method_get_param_count(saveBank);
 void* args[1]={};float f=static_cast<float>(desired);double d=static_cast<double>(desired);int i=desired;int64_t l=desired;
 if(n==1){
  switch(api.type_get_type(api.method_get_param(saveBank,0))){
   case TYPE_I4:case TYPE_U4:args[0]=&i;break;
   case TYPE_I8:case TYPE_U8:args[0]=&l;break;
   case TYPE_R4:args[0]=&f;break;
   case TYPE_R8:args[0]=&d;break;
   default:InterlockedExchange(&state->goldDataStatus,-2);return false;
  }
 }else if(n!=0){InterlockedExchange(&state->goldDataStatus,-2);return false;}
 IlObj* exception=nullptr;
 InterlockedExchange(&state->goldDataStatus,1);
 api.runtime_invoke(saveBank,controller,n==1?args:nullptr,&exception);
 if(exception){InterlockedExchange(&state->goldDataStatus,-1);return false;}
 InterlockedExchange(&state->goldDataStatus,2);
 return true;
}
int ReadDataBank(IlObj* controller){
 if(!readBankFromData||api.method_get_param_count(readBankFromData)!=0)return -1;
 int rt=api.type_get_type(api.method_get_return_type(readBankFromData));
 if(rt!=TYPE_I4&&rt!=TYPE_R4&&rt!=TYPE_R8&&rt!=TYPE_I8)return -1;
 IlObj* exception=nullptr;auto result=api.runtime_invoke(readBankFromData,controller,nullptr,&exception);
 if(exception||!result)return -1;
 void* unboxed=api.object_unbox(result);if(!unboxed)return -1;
 if(rt==TYPE_I4)return *static_cast<int*>(unboxed);
 if(rt==TYPE_R4)return static_cast<int>(*static_cast<float*>(unboxed));
 if(rt==TYPE_R8)return static_cast<int>(*static_cast<double*>(unboxed));
 return static_cast<int>(*static_cast<int64_t*>(unboxed));
}
void ProcessGold(IlObj* controller,bool gameThread){
 if(state&&state->quietExit)return;
 if(!controller||!state||InterlockedCompareExchange(&goldLock,1,0)!=0)return;
 auto sequence=state->goldSequence;
 if(sequence>0&&sequence!=state->goldRuntimeSequence){
  int wanted=state->requestedGold;
  if(wanted>=0&&wanted<=1000000000&&WriteBank(controller,wanted)){
   int real=ReadBank(controller);
   InterlockedExchange(&state->observedGold,real);
   if(real==wanted){
    InterlockedExchange(&state->goldRuntimeSequence,sequence);
    bool called=InvokeBankSave(controller,wanted);
    // GameData verification is separate from runtime-field verification.
    int data=called?ReadDataBank(controller):-1;
    InterlockedExchange(&state->goldDataReadback,data);
    if(called&&data>=0&&data==wanted){
     InterlockedExchange(&state->goldSavedSequence,sequence);
     Status("运行时字段与 GameData 均已验证，磁盘保存仍需重启确认。");
    }else if(called){Status("游戏数据写入方法已调用，但 GameData 未验证；请检查实际画面。");}
    else{Status("运行时字段已改变，但 GameData 同步方法不可用；可能被游戏覆盖。");}
   }else{InterlockedExchange(&state->lastError,205);Status("运行时金币字段读回不匹配，未标记成功。");}
  }else{InterlockedExchange(&state->lastError,201);Status("未找到支持的金币字段，已跳过写入。");}
 }
 if(gameThread&&state->goldRuntimeSequence>0&&refreshTexts){
  // Invoke only in an actual game callback; never from the helper's worker thread.
  // Do not recurse if refreshing text itself invokes a hook.
  if(goldRefreshSequence!=state->goldRuntimeSequence){
   InterlockedExchange(&goldRefreshSequence,state->goldRuntimeSequence);
   IlObj* error=nullptr;api.runtime_invoke(refreshTexts,controller,nullptr,&error);
   if(!error)InterlockedIncrement(&state->goldRefreshCalls);
  }
 }
 InterlockedExchange(&goldLock,0);
}
// A Unity List<DICE> or an array of DICE, read only during game's result events.
struct ManagedArray {void* klass;void* monitor;void* bounds;uintptr_t length;IlObj* elements[1];};
int DiceList(IlObj* diceController,IlObj** output,int max){
 if(!diceController||!spawnedField)return 0;
 IlObj* container=nullptr;api.field_get_value(diceController,spawnedField,&container);
 if(!container||!IsReadable(container,32))return 0;
 IlObj* arrayObject=nullptr;int count=0;
 IlClass* klass=api.object_get_class(container);
 IlField* items=klass?api.class_get_field_from_name(klass,"_items"):nullptr;
 IlField* size=klass?api.class_get_field_from_name(klass,"_size"):nullptr;
 if(items&&size){
  api.field_get_value(container,items,&arrayObject);
  api.field_get_value(container,size,&count);
 }else{
  // Reject unknown container classes; only a managed DICE[] may be cast.
  const char* typenameText=klass?api.class_get_name(klass):nullptr;
  if(!typenameText||strcmp(typenameText,"DICE[]")!=0)return 0;
  arrayObject=container;
  auto* arr=reinterpret_cast<ManagedArray*>(arrayObject);
  if(IsReadable(arr,32))count=static_cast<int>(arr->length);
 }
 if(count<1||count>max||!arrayObject)return 0;
 auto* arr=reinterpret_cast<ManagedArray*>(arrayObject);
 if(!IsReadable(arr,32+sizeof(void*)*static_cast<size_t>(count))||arr->length<static_cast<size_t>(count)||arr->length>1024)return 0;
 for(int n=0;n<count;++n)output[n]=arr->elements[n];
 return count;
}
void ForceAll(IlObj* diceController){
 if(!state||state->quietExit||state->diceMode==0||!forceDice||!finalField||inDice)return;
 const int face=GetFace();if(face<1||face>6)return;
 IlObj* dices[32]{};int count=DiceList(diceController,dices,32);
 if(count!=5){InterlockedIncrement(&state->badDiceCollection);return;}
 InterlockedExchange(&state->topCalls,count);
 inDice=true;
 for(int j=0;j<count;++j){
  auto dice=dices[j];if(!dice||!IsReadable(dice,16))continue;
  auto k=api.object_get_class(dice);const char* typeName=k?api.class_get_name(k):nullptr;
  if(!typeName||strcmp(typeName,"DICE")!=0)continue;
  // Do not interrupt the physics simulation. Only modify settled dice.
  if(stoppedField){unsigned char stopped=0;api.field_get_value(dice,stoppedField,&stopped);if(!stopped)continue;}
  InterlockedIncrement(&state->diceIntercepts);
  if(ReadI4(dice,finalField)==face)continue;
  int argument=face;void* args[]={&argument};IlObj* exception=nullptr;
  api.runtime_invoke(forceDice,dice,args,&exception);
  if(!exception&&ReadI4(dice,finalField)==face)InterlockedIncrement(&state->diceForced);
  else{InterlockedIncrement(&state->forceMismatch);if(exception)InterlockedExchange(&state->lastError,301);}
 }
 inDice=false;
}

// User-requested editor maps to the *serializable GameData fields* where present.
// Derived properties are read only: writing upgrade points != assigning a final stat.
struct FeatureSpec {const char* field;const char* property;bool editable;};
constexpr FeatureSpec features[32]={
 {"_relationsLura","relationsLura",true},{"_relationsRiya","relationsRiya",true},
 {"lvl",nullptr,true},{"points_up",nullptr,true},{"exp","exp",true},
 {"hp",nullptr,true},{"hp_point",nullptr,true},{"hp_reg_point",nullptr,true},
 {"mana",nullptr,true},{"mana_reg_point",nullptr,true},{"speed_point",nullptr,true},
 {"def_point",nullptr,true},{"kin_damage_point",nullptr,true},
 {"sword_damage_point",nullptr,true},{"bow_damage_point",nullptr,true},
 {"bow_speed_point",nullptr,true},{"crossBow_damage_point",nullptr,true},
 {"crossBow_speed_point",nullptr,true},
 {"exp_nextLvl","exp_nextLvl",false},{"hp_max","hp_max",false},
 {"hp_reg","hp_reg",false},{"mana_reg","mana_reg",false},
 {"speed","speed",false},{"def","def",false},
 {"kin_damage","kin_damage",false},{"sword_damage","sword_damage",false},
 {"bow_damage","bow_damage",false},{"bow_speed","bow_speed",false},
 {"crossBow_damage","crossBow_damage",false},{"crossBow_speed","crossBow_speed",false},
 {"relationsLura","relationsLura",false},{"relationsRiya","relationsRiya",false}
};
void FeatureStatus(const char* msg){if(state)strncpy_s(state->featureMessage,sizeof(state->featureMessage),msg,_TRUNCATE);}
// Some builds expose GameData through a static MegaData property. Never invoke
// an instance getter with a null receiver or treat an unknown struct as a ref.
IlObj* CurrentData(){
 if(!gameDataGetter){
  // Some builds expose the singleton directly rather than through MegaData.
  if(dataClass){
   auto singleton=api.class_get_field_from_name(dataClass,"<Instance>k__BackingField");
   if(!singleton)singleton=api.class_get_field_from_name(dataClass,"_instance");
   if(singleton&&(api.field_get_flags(singleton)&0x10)!=0){
    IlObj* instance=nullptr;api.field_static_get_value(singleton,&instance);
    if(instance&&IsReadable(instance,16)){if(state)InterlockedExchange(&state->dataSeen,1);return instance;}
   }
  }
  return nullptr;
 }
 if(api.method_get_flags){unsigned int iFlags=0;const unsigned int flags=api.method_get_flags(gameDataGetter,&iFlags);
  if((flags&0x10)==0)return nullptr;}
 int kind=api.type_get_type(api.method_get_return_type(gameDataGetter));
 if(kind!=0x12&&kind!=0x1c&&kind!=0x1d)return nullptr;
 IlObj* exception=nullptr;
 IlObj* obj=api.runtime_invoke(gameDataGetter,nullptr,nullptr,&exception);
 if(exception||!obj||!IsReadable(obj,sizeof(void*)*2))return nullptr;
 if(state)InterlockedExchange(&state->dataSeen,1);
 return obj;
}
// Exactly 32-bit numerical snapshots. Larger numeric types are deliberately
// not truncated or written, because that could corrupt the managed object.
int GetRawField(IlObj* data,IlField* f,LONG& out){
 if(!data||!f)return 0;
 int type=FieldType(f);
 if(type==TYPE_I4||type==TYPE_U4||type==TYPE_R4){
  int raw=0;api.field_get_value(data,f,&raw);out=raw;return type;
 }
 return 0;
}
int GetRawProperty(IlObj* data,const char* name,LONG& out){
 if(!data||!name||!dataClass)return 0;
 char getter[96];_snprintf_s(getter,sizeof getter,_TRUNCATE,"get_%s",name);
 auto m=api.class_get_method_from_name(dataClass,getter,0);
 if(!m)return 0;
 int t=api.type_get_type(api.method_get_return_type(m));
 if(t!=TYPE_I4&&t!=TYPE_U4&&t!=TYPE_R4)return 0;
 IlObj* exception=nullptr;IlObj* boxed=api.runtime_invoke(m,data,nullptr,&exception);
 if(exception||!boxed)return 0;
 void* raw=api.object_unbox(boxed);if(!raw||!IsReadable(raw,4))return 0;
 int bits=0;memcpy(&bits,raw,4);out=bits;return t;
}
void TakeSnapshot(IlObj* data){
 if(!state||!data)return;
 for(int i=0;i<32;++i){
  LONG bits=0;int kind=0;
  // For computed properties, ask the actual getter. For raw fields, use
  // the backing field; if absent fall back to the named getter.
  if(features[i].editable){
   auto f=api.class_get_field_from_name(dataClass,features[i].field);
   kind=GetRawField(data,f,bits);
  }
  if(!kind)kind=GetRawProperty(data,features[i].property,bits);
  InterlockedExchange(&state->snapshotValues[i],bits);
  InterlockedExchange(&state->snapshotTypes[i],kind);
 }
 InterlockedExchange(&state->snapshotApplied,state->snapshotSequence);
}

// For a calculated stat, solve the game's actual getter against the matching
// upgrade-point field. Never claim success unless the getter returns the target.
// Unsupported/non-monotonic targets are rolled back to their previous points.
void ApplyCalculatedTarget(IlObj* data,int index,int valueBits){
 if(index<19||index>29){InterlockedExchange(&state->featureStatus,-2);FeatureStatus("此计算属性不允许直接设置目标。");return;}
 // bow/crossbow mapped explicitly to prevent an off-by-one mutation.
 constexpr int backingIndex[11]={6,7,9,10,11,12,13,14,15,16,17};
 const char* backing=features[backingIndex[index-19]].field;
 IlField* field=api.class_get_field_from_name(dataClass,backing);
 if(!field||FieldType(field)!=TYPE_I4||(api.field_get_flags(field)&0x10)!=0){
  InterlockedExchange(&state->featureStatus,-2);FeatureStatus("对应升级点不是可编辑的 int32 字段，已拒绝自动反推。");return;
 }
 LONG originalRaw=0;
 if(GetRawField(data,field,originalRaw)!=TYPE_I4){InterlockedExchange(&state->featureStatus,-2);return;}
 LONG previousValue=0;
 int returnType=GetRawProperty(data,features[index].property,previousValue);
 if(returnType!=TYPE_I4&&returnType!=TYPE_R4){
  InterlockedExchange(&state->featureStatus,-2);FeatureStatus("游戏计算属性的返回类型不支持反推。");return;
 }
 const auto value=[](LONG bits,int typ)->double{
  if(typ==TYPE_R4){float v=0;memcpy(&v,&bits,4);return static_cast<double>(v);}
  return static_cast<double>(bits);
 };
 const double target=value(valueBits,returnType);
 if(!std::isfinite(target)||target<0||target>100000000){InterlockedExchange(&state->featureStatus,-2);return;}
 const auto same=[&](double v)->bool{
  return returnType==TYPE_I4?v==target:std::fabs(v-target)<=0.0001;
 };
 auto check=[&](int pts,double& out)->bool{
  api.field_set_value(data,field,&pts);
  LONG raw=0;int type=GetRawProperty(data,features[index].property,raw);
  if(type!=returnType)return false;
  out=value(raw,returnType);return std::isfinite(out);
 };
 const auto restore=[&](){int orig=originalRaw;api.field_set_value(data,field,&orig);};
 if(same(value(previousValue,returnType))){
  InterlockedExchange(&state->featureStatus,1);InterlockedExchange(&state->featureApplied,state->featureSequence);
  InterlockedExchange(&state->featureReadback,previousValue);InterlockedExchange(&state->featureType,returnType);
  FeatureStatus("当前计算属性已等于目标值，无需更改升级点。");return;
 }
 // First sample both ends to determine whether the formula is monotonic.
 const int upperBound=100000;
 double lowValue=0,highValue=0;
 if(!check(0,lowValue)||!check(upperBound,highValue)||lowValue==highValue){
  restore();InterlockedExchange(&state->featureStatus,-5);FeatureStatus("计算公式无法安全反推；升级点已回退。");return;
 }
 bool increasing=highValue>lowValue;
 if((increasing&&(target<lowValue||target>highValue))||
    (!increasing&&(target>lowValue||target<highValue))){
  restore();InterlockedExchange(&state->featureStatus,-5);FeatureStatus("请求的最终数值超出可反推范围；升级点已回退。");return;
 }
 int lo=0,hi=upperBound,best=-1;
 for(int k=0;k<19&&lo<=hi;++k){
  int mid=lo+(hi-lo)/2;double observed=0;
  if(!check(mid,observed)){restore();InterlockedExchange(&state->featureStatus,-5);FeatureStatus("属性 Getter 不稳定，升级点已回退。");return;}
  if(same(observed)){best=mid;break;}
  if((observed<target)==increasing)lo=mid+1;else hi=mid-1;
 }
 if(best<0){restore();InterlockedExchange(&state->featureStatus,-5);FeatureStatus("升级点无法精确匹配目标值，已回退。");return;}
 double confirm=0;
 if(!check(best,confirm)||!same(confirm)){
  restore();InterlockedExchange(&state->featureStatus,-5);FeatureStatus("最终值复核失败，升级点已回退。");return;
 }
 LONG confirmed=0;GetRawProperty(data,features[index].property,confirmed);
 InterlockedExchange(&state->featureReadback,confirmed);InterlockedExchange(&state->featureType,returnType);
 InterlockedIncrement(&state->featureWrites);
 InterlockedExchange(&state->featureStatus,1);InterlockedExchange(&state->featureApplied,state->featureSequence);
 FeatureStatus("已通过游戏 Getter 验证最终目标；升级点已写入。保存仍需实机确认。");
 TakeSnapshot(data);
}

void ApplyFeature(IlObj* data){
 if(!state||!data)return;
 const int index=state->featureIndex;
 const int raw=state->featureValueBits;
 if(index>=19&&index<=29){ApplyCalculatedTarget(data,index,raw);return;}
 if(index<0||index>=32||!features[index].editable){
  InterlockedExchange(&state->featureStatus,-2);FeatureStatus("只读/无效的字段，拒绝写入。");return;
 }
 const auto& spec=features[index];
 auto f=api.class_get_field_from_name(dataClass,spec.field);
 int kind=FieldType(f);
 const bool useProperty=(index==0||index==1||index==4);
 const IlMethod* setter=nullptr;
 if(useProperty&&spec.property){
  char setterName[96];_snprintf_s(setterName,sizeof(setterName),_TRUNCATE,"set_%s",spec.property);
  setter=api.class_get_method_from_name(dataClass,setterName,1);
 }
 if((kind!=TYPE_I4&&kind!=TYPE_U4&&kind!=TYPE_R4)&&setter){
  // e.g. 'exp' is exposed as a property in some game builds.
  kind=api.type_get_type(api.method_get_param(setter,0));
 }
 if(kind!=TYPE_I4&&kind!=TYPE_U4&&kind!=TYPE_R4){
  InterlockedExchange(&state->featureStatus,-1);
  FeatureStatus("当前版本不存在该原始字段或受支持的 Setter，未执行修改。");return;
 }
 if(f&&(api.field_get_flags(f)&0x10)!=0){
  InterlockedExchange(&state->featureStatus,-2);FeatureStatus("静态字段不支持修改。");return;
 }
 if(kind==TYPE_R4){float v=0;memcpy(&v,&raw,4);if(!(v>=0.0f&&v<=100000000.0f)){
   InterlockedExchange(&state->featureStatus,-2);FeatureStatus("浮点数值超出支持范围。");return;}}
 else if(raw<0||raw>100000000){InterlockedExchange(&state->featureStatus,-2);FeatureStatus("数值超出支持范围。");return;}
 bool calledSetter=false;
 if(setter&&api.type_get_type(api.method_get_return_type(setter))==TYPE_VOID&&
    api.type_get_type(api.method_get_param(setter,0))==kind){
  int dataBits=raw;void* args[1]={&dataBits};IlObj* exception=nullptr;
  api.runtime_invoke(setter,data,args,&exception);
  if(!exception)calledSetter=true;
 }
 if(!calledSetter){
  if(!f||FieldType(f)!=kind){InterlockedExchange(&state->featureStatus,-1);FeatureStatus("无法写入字段或属性 Setter，未执行修改。");return;}
  int dataBits=raw;api.field_set_value(data,f,&dataBits);
 }
 LONG read=0;int rt=0;
 if(useProperty&&spec.property)rt=GetRawProperty(data,spec.property,read);
 if(!rt&&f)rt=GetRawField(data,f,read);
 InterlockedExchange(&state->featureType,rt);
 InterlockedExchange(&state->featureReadback,read);
 if(rt!=kind||read!=raw){
  InterlockedExchange(&state->featureStatus,-4);
  FeatureStatus("写入后游戏返回的值与请求不一致，可能存在范围限制或额外变换。");
  TakeSnapshot(data);return;
 }
 InterlockedIncrement(&state->featureWrites);
 InterlockedExchange(&state->featureStatus,1);
 InterlockedExchange(&state->featureApplied,state->featureSequence);
 FeatureStatus("GameData 读回成功；最终画面和磁盘存档尚需验证。");
 TakeSnapshot(data);
}
void ServiceFeatures(){
 if(!state||state->quietExit)return;
 LONG req=state->featureSequence;
 LONG snap=state->snapshotSequence;
 if(req==state->featureApplied&&snap==state->snapshotApplied)return;
 IlObj* data=CurrentData();
 if(!data){
  InterlockedExchange(&state->featureStatus,-3);FeatureStatus("无法获取当前 GameData；请进入游戏场景后重试。");
  // A new request will be processed if the button is pressed again.
  if(req!=state->featureApplied)InterlockedExchange(&state->featureApplied,req);
  if(snap!=state->snapshotApplied)InterlockedExchange(&state->snapshotApplied,snap);
  return;
 }
 if(req!=state->featureApplied){ApplyFeature(data);InterlockedExchange(&state->featureApplied,req);}
 if(snap!=state->snapshotApplied)TakeSnapshot(data);
}

using OrigVoid=void (__fastcall*)(IlObj*,const IlMethod*);
using OrigSelect=void (__fastcall*)(IlObj*,int,const IlMethod*);
OrigVoid originalCollect=nullptr,originalRefresh=nullptr,originalUi=nullptr,originalAllStopped=nullptr,originalFinish=nullptr;
OrigSelect originalSelect=nullptr,originalSelectInstant=nullptr;
void __fastcall HookCollect(IlObj* controller,const IlMethod* method){
 InterlockedIncrement(&state->finalCalls);
 if(!state->quietExit&&state->diceMode)ForceAll(controller);
 originalCollect(controller,method);
}
void __fastcall HookRefresh(IlObj* controller,const IlMethod* method){
 InterlockedIncrement(&state->stoppedCalls);
 if(!state->quietExit&&state->diceMode)ForceAll(controller);
 originalRefresh(controller,method);
}
void __fastcall HookAllStopped(IlObj* controller,const IlMethod* method){
 InterlockedIncrement(&state->allStoppedCalls);
 if(!state->quietExit&&state->diceMode)ForceAll(controller);
 originalAllStopped(controller,method);
}
void __fastcall HookFinish(IlObj* controller,const IlMethod* method){
 InterlockedIncrement(&state->finishCalls);
 if(!state->quietExit&&state->diceMode)ForceAll(controller);
 originalFinish(controller,method);
}
void __fastcall HookSelect(IlObj* selector,int value,const IlMethod* method){
 InterlockedIncrement(&state->selectCalls);
 originalSelect(selector,value,method);
 if(!state->quietExit)ObserveSelection(value,1);
}
void __fastcall HookSelectInstant(IlObj* selector,int value,const IlMethod* method){
 InterlockedIncrement(&state->selectInstantCalls);
 originalSelectInstant(selector,value,method);
 if(!state->quietExit)ObserveSelection(value,2);
}
void __fastcall HookUI(IlObj* controller,const IlMethod* method){
 originalUi(controller,method);
 if(!state->quietExit&&state->goldSequence>state->goldRuntimeSequence)ProcessGold(controller,true);
 else if(!state->quietExit&&state->goldRuntimeSequence>0&&goldRefreshSequence!=state->goldRuntimeSequence)ProcessGold(controller,true);
}

bool InstallHooks(){
 IlClass* dice=FindClass("DICE"),*game=FindClass("LR_GameController"),
  *dices=FindClass("LR_DicesController"),*selector=FindClass("LR_ValueSelector");
 if(!dice||!game||!dices||!selector){Status("所需类尚未装载或与当前游戏版本不一致。");return false;}
 finalField=api.class_get_field_from_name(dice,"finalValue");
 topField=api.class_get_field_from_name(dice,"topFaceNumber");
 stoppedField=api.class_get_field_from_name(dice,"isStopped");
 spawnedField=api.class_get_field_from_name(dices,"spawnedDices");
 forceDice=api.class_get_method_from_name(dice,"ForceSetValueInstant",1);
 selectedField=api.class_get_field_from_name(game,"activeSelectedValue");
 bankField=api.class_get_field_from_name(game,"playerBank");
 activeField=api.class_get_field_from_name(game,"activeInstance");
 bankType=FieldType(bankField);
 dataClass=FindClass("GameData");
 if(auto mega=FindClass("MegaData"))gameDataGetter=api.class_get_method_from_name(mega,"get_GameData",0);
 if(!gameDataGetter&&dataClass)gameDataGetter=api.class_get_method_from_name(dataClass,"get_Instance",0);
 InterlockedExchange(&state->featuresAvailable,dataClass?1:0);
 saveBank=api.class_get_method_from_name(game,"SetPlayerBankToData",-1);
 readBankFromData=api.class_get_method_from_name(game,"GetPlayerBankFromData",-1);
 refreshTexts=api.class_get_method_from_name(game,"RefreshAllTextsInstant",0);
 int saveArgs=saveBank?static_cast<int>(api.method_get_param_count(saveBank)):-1;
 int saveArgType=saveArgs==1?api.type_get_type(api.method_get_param(saveBank,0)):0;
 int readType=readBankFromData?api.type_get_type(api.method_get_return_type(readBankFromData)):0;
 int flags=0;
 auto hooking=[&](IlClass* c,const char* name,int argc,int expectedArg,int bit,void* callback,void** orig){
  if(!c)return;
  auto m=api.class_get_method_from_name(c,name,argc);
  if(!m||api.type_get_type(api.method_get_return_type(m))!=TYPE_VOID||
     (argc==1&&api.type_get_type(api.method_get_param(m,0))!=expectedArg))return;
  if(Install(m,callback,orig))flags|=bit;
  else InterlockedIncrement(&state->hookError);
 };
 hooking(dices,"CollectResults",0,0,1,(void*)&HookCollect,(void**)&originalCollect);
 hooking(dices,"RefreshResultsFromSpawnedDices",0,0,2,(void*)&HookRefresh,(void**)&originalRefresh);
 hooking(dices,"HandleAllDicesStopped",0,0,32,(void*)&HookAllStopped,(void**)&originalAllStopped);
 hooking(dices,"FinishResult",0,0,64,(void*)&HookFinish,(void**)&originalFinish);
 hooking(selector,"SelectValue",1,TYPE_I4,4,(void*)&HookSelect,(void**)&originalSelect);
 hooking(selector,"SelectValueInstant",1,TYPE_I4,8,(void*)&HookSelectInstant,(void**)&originalSelectInstant);
 hooking(game,"UpdateAllButtonAvailability",0,0,16,(void*)&HookUI,(void**)&originalUi);
 InterlockedExchange(&state->initialized,flags);
 _snprintf_s(state->report,sizeof(state->report),_TRUNCATE,
  "Collect=%s | Refresh=%s | AllStopped=%s | Finish=%s\nSelect=%s | SelectInstant=%s | UI=%s\n"
  "DiceList=%d; FinalField=%d; StopField=%d; ForceMethod=%d\n"
  "BankType=0x%02X; BankSetterArgs=%d; BankSetterArgType=0x%02X; BankDataGetterType=0x%02X; ActiveStatic=%d; GameData=%d; ABI=5",
  flags&1?"hook":"miss",flags&2?"hook":"miss",flags&32?"hook":"miss",flags&64?"hook":"miss",
  flags&4?"hook":"miss",flags&8?"hook":"miss",flags&16?"hook":"miss",
  spawnedField?1:0,finalField?1:0,stoppedField?1:0,forceDice?1:0,bankType,
  saveArgs,saveArgType,readType,activeField&&api.field_get_flags(activeField)&0x10?1:0,
  dataClass?1:0);
 InterlockedExchange(&state->ready,1);
 Status(flags?"结果/选择事件 Hook 已安装；等待真实骰子结算验证。":"实际结算 Hook 未安装，关闭修改并查看诊断。");
 return true;
}
DWORD WINAPI Worker(void*){
 wchar_t name[100];MakeMappingName(GetCurrentProcessId(),name);
 for(int i=0;i<160&&!mapping;++i){mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!mapping)Sleep(250);}
 if(!mapping)return 0;
 state=(SharedState*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState));
 if(!state||state->magic!=LKA_MAGIC||state->abi!=LKA_ABI)return 0;
 InterlockedExchange(&state->goldDataReadback,-1);
 wchar_t eventName[120];swprintf_s(eventName,L"Local\\LuckyKissWakeV5_%lu",(unsigned long)GetCurrentProcessId());
 workEvent=CreateEventW(nullptr,FALSE,FALSE,eventName);
 if(!workEvent){Status("无法创建事件同步对象。");return 0;}
 HMODULE gameDll=nullptr;
 for(int i=0;i<240&&!gameDll;++i){gameDll=GetModuleHandleW(L"GameAssembly.dll");if(!gameDll)Sleep(250);}
 if(!gameDll||!InitApi(gameDll)){Status("IL2CPP 导出表缺失。");InterlockedExchange(&state->lastError,102);InterlockedExchange(&state->ready,1);return 0;}
 IlDomain* domain=nullptr;
 for(int i=0;i<240&&!domain;++i){domain=api.domain_get();if(!domain)Sleep(250);}
 if(!domain){Status("无法读取 IL2CPP domain。");InterlockedExchange(&state->lastError,103);InterlockedExchange(&state->ready,1);return 0;}
 IlThread* attached=api.thread_attach(domain);
 if(!attached){Status("无法附加 IL2CPP 工作线程。");InterlockedExchange(&state->lastError,103);InterlockedExchange(&state->ready,1);return 0;}
 for(int i=0;i<160;++i){if(InstallHooks())break;Sleep(250);}
 // v0.0.3 kept an IL2CPP-attached thread alive, polling forever. Detach
 // before waiting, so the shutdown path has no long-running managed thread.
 api.thread_detach(attached);attached=nullptr;
 InterlockedExchange(&state->workerIdle,1);
 // No periodic game memory reads. The frontend sends a named Windows event
 // only for explicit actions. The thread stays *unattached* while idle.
 while(state&&state->magic==LKA_MAGIC){
  DWORD result=WaitForSingleObject(workEvent,INFINITE);
  if(result!=WAIT_OBJECT_0||!state||state->quietExit)break;
  auto d=api.domain_get();if(!d)break;
  attached=api.thread_attach(d);if(!attached)break;
  InterlockedExchange(&state->workerIdle,0);
  if(state->goldSequence>state->goldRuntimeSequence){
   if(auto c=ActiveController())ProcessGold(c,false);
  }
  ServiceFeatures();
  // Detach immediately, even after a field validation failure.
  api.thread_detach(attached);attached=nullptr;
  InterlockedExchange(&state->workerIdle,1);
 }
 // 2 = permanently stopped. Do not report a terminated worker as busy.
 InterlockedExchange(&state->workerIdle,2);
 CloseHandle(workEvent);workEvent=nullptr;
 return 0;
}
}
BOOL WINAPI DllMain(HMODULE m,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(m);HANDLE t=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(t)CloseHandle(t);}
 return TRUE;
}
