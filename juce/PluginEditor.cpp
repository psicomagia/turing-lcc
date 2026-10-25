#include "PluginEditor.h"
#include "BinaryData.h"

// fetch('/estado'), '/cfg', '/acao', '/pastas', '/amostra' -> núcleo, pela função nativa "tm"
static const char* kPonte = R"JS(<script>
(function(){
  const J=window.__JUCE__, pend=new Map(); let id=1000000;
  if(J&&J.backend)J.backend.addEventListener('__juce__complete',d=>{const r=pend.get(d.promiseId);if(r){pend.delete(d.promiseId);r(d.result);}});
  const call=a=>new Promise(res=>{if(!J||!J.backend){res(null);return;}const k=id++;pend.set(k,res);
    J.backend.emitEvent('__juce__invoke',{name:'tm',params:[a],resultId:k});});
  const of=window.fetch?window.fetch.bind(window):null;
  window.fetch=async function(u,o){
    const url=typeof u==='string'?u:(u&&u.url)||'', s=url.replace(/^[a-z]+:\/\/[^/]*/i,''), qi=s.indexOf('?');
    const path=qi<0?s:s.slice(0,qi), q=qi<0?'':s.slice(qi+1);
    if(!['/estado','/cfg','/acao','/pastas','/amostra'].includes(path))return of(u,o);
    const r=await call(JSON.stringify({m:((o&&o.method)||'GET').toUpperCase(),path,q,body:o&&o.body!=null?String(o.body):''}));
    if(!r)return new Response('sem plugin',{status:503});
    return new Response(r.body,{status:r.status,headers:{'Content-Type':r.type||'text/plain'}});
  };
  const mem={};   // localStorage dentro do plugin: só na sessão
  try{Object.defineProperty(window,'localStorage',{value:{getItem:k=>k in mem?mem[k]:null,setItem:(k,v)=>{mem[k]=String(v);},removeItem:k=>{delete mem[k];}},configurable:true});}catch(e){}
})();
</script>)JS";

juce::String TmEditor::page()
{
    auto html = juce::String::fromUTF8 (BinaryData::tm_html, BinaryData::tm_htmlSize);
    return html.replaceFirstOccurrenceOf ("<head>", "<head>\n" + juce::String (kPonte));
}

static std::vector<std::byte> toBytes (const juce::String& s)
{
    const auto u = s.toUTF8();
    const auto n = u.sizeInBytes() - 1;
    std::vector<std::byte> v (n);
    std::memcpy (v.data(), u.getAddress(), n);
    return v;
}

TmEditor::TmEditor (TmProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      web (juce::WebBrowserComponent::Options {}
               .withNativeIntegrationEnabled()
               .withKeepPageLoadedWhenBrowserIsHidden()
               .withNativeFunction ("tm", [this] (const juce::Array<juce::var>& a, juce::WebBrowserComponent::NativeFunctionCompletion done)
                                    { done (proc.http (a.size() > 0 ? a[0].toString() : juce::String())); })
               .withResourceProvider ([] (const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource>
                                      {
                                          if (url == "/" || url.startsWith ("/index.html"))
                                              return juce::WebBrowserComponent::Resource { toBytes (page()), "text/html" };
                                          return std::nullopt;
                                      }))
{
    addAndMakeVisible (web);
    setResizable (true, true);
    setResizeLimits (640, 480, 2600, 2000);
    setSize (1180, 860);
   #if JUCE_LINUX
    // no Linux o JUCE passa recursos por um pipe pequeno: abre a página de um arquivo temporário
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TuringLCC").getChildFile ("index.html");
    f.getParentDirectory().createDirectory();
    f.replaceWithText (page());
    web.goToURL (juce::URL (f).toString (false));
   #else
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif
}
