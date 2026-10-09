(function(){
  if(window.bleLockWizardSpeechInstalled||typeof say!=='function'||!window.BleLockWizardSpeech)return;
  window.bleLockWizardSpeechInstalled=true;
  let completion=null;
  window.onBleLockSpeechDone=function(token,error){
    if(token!==speechToken)return;
    let done=completion;
    completion=null;
    if(error){
      let speechStatus=document.getElementById('status');
      speechStatus.textContent=error;
      speechStatus.className='data bad';
      trace('speech_error');
      return;
    }
    if(done)done();
  };
  window.say=function(display=displayTexts[stage],speech=speechTexts[stage],done){
    instruction.textContent=display;
    collecting=false;
    completion=done;
    let token=++speechToken;
    if('speechSynthesis'in window)speechSynthesis.cancel();
    BleLockWizardSpeech.speak(speech,token);
  };
  const originalExpired=expired;
  window.expired=function(){
    // An in-flight tick can observe calibration_mode=false after a successful save.
    if(finished)return;
    completion=null;
    BleLockWizardSpeech.cancel();
    originalExpired();
  };
  const originalCancel=cancelWizard;
  window.cancelWizard=async function(){
    completion=null;
    speechToken++;
    BleLockWizardSpeech.cancel();
    await originalCancel();
    if(!window.closed)window.location.replace('/');
  };
  addEventListener('beforeunload',()=>{completion=null;BleLockWizardSpeech.cancel()});
  // The page may have started its browser fallback before onPageFinished.
  if(!finished&&instruction.textContent!=='Запуск…'){
    samples=[];
    say(displayTexts[stage],speechTexts[stage],stage<2?beginCollecting:closeCompletedWizard);
  }
})();
