'use strict';
const $=id=>document.getElementById(id);
let state=null,busy=false,targetKey=null,frame=-1,loading=false;
function message(text){$('feedback').textContent=text;}
function availability(){
 const alive=Boolean(state?.alive), blocked=busy||!state;
 document.querySelectorAll('button,input,textarea').forEach(el=>el.disabled=blocked||!alive);
 $('target').disabled=blocked;$('restart').disabled=blocked;
 $('step').disabled=blocked||!alive||state.running;
}
function render(next){
 state=next;
 $('connection').textContent=next.error?'Runner needs attention':next.running?'Running locally':next.alive?'Paused locally':'Runner stopped';
 $('toggle').textContent=next.running?'Pause':'Resume';
 if(targetKey!==next.target){
  targetKey=next.target;
  $('target').replaceChildren(...next.targets.map(t=>{const o=document.createElement('option');o.value=t.key;o.textContent=t.key;return o;}));
  $('target').value=next.target;
  const board=next.targets.find(t=>t.key===next.target);
  $('board-name').textContent=board?.name||'';
  $('buttons').replaceChildren(...(board?.controls||[]).map(control=>{const b=document.createElement('button');b.textContent=control.label;b.title='Press the '+control.label+' board button';b.onclick=()=>action({action:'button',which:control.label});return b;}));
  $('battery-form').hidden=!board?.battery;
 }
 if(next.frame!==frame){frame=next.frame;$('display').src='/frame.png?v='+frame;}
 $('display').hidden=next.width===0;$('empty').hidden=next.width>0;
 $('dimensions').textContent=next.width?`${next.width} × ${next.height} / firmware framebuffer`:'No frame yet';
 $('x').max=String(Math.max(0,next.width-1));$('y').max=String(Math.max(0,next.height-1));
 const logs=next.logs||'No serial output yet.';
 if($('logs').textContent!==logs){const end=$('logs').scrollTop+$('logs').clientHeight>=$('logs').scrollHeight-10;$('logs').textContent=logs;if(end)$('logs').scrollTop=$('logs').scrollHeight;}
 if(next.error)message(next.error);
 availability();
}
async function refresh(){
 if(loading)return;loading=true;
 try{const response=await fetch('/api/state',{signal:AbortSignal.timeout(15000)});if(!response.ok)throw Error('Studio unavailable. Restart the local server.');render(await response.json());}
 catch(error){$('connection').textContent='Studio disconnected';message(error.message);state=null;availability();}
 finally{loading=false;}
}
async function action(data){
 if(busy||!state)return;
 busy=true;availability();message('Working...');
 try{
  const response=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json','X-Esprite-Token':state.token},body:JSON.stringify(data),signal:AbortSignal.timeout(30000)});
  const result=await response.json();if(!response.ok)throw Error(result.error);
  message(data.action==='target'?'Fresh session opened.':'Done.');
 }catch(error){message(error.message);}
 finally{busy=false;await refresh();availability();}
}
$('target').onchange=()=>action({action:'target',target:$('target').value});
$('restart').onclick=()=>action({action:'target',target:state.target});
$('toggle').onclick=()=>action({action:'running',value:!state.running});
$('step').onclick=()=>action({action:'step'});
$('display').onclick=event=>{if(!state?.alive||busy)return;const r=event.currentTarget.getBoundingClientRect();action({action:'tap',x:Math.min(state.width-1,Math.floor((event.clientX-r.left)*state.width/r.width)),y:Math.min(state.height-1,Math.floor((event.clientY-r.top)*state.height/r.height))});};
$('touch').onsubmit=event=>{event.preventDefault();action({action:'tap',x:Number($('x').value),y:Number($('y').value)});};
$('serial').onsubmit=event=>{event.preventDefault();action({action:'serial',text:$('serial-text').value});};
$('battery-form').onsubmit=event=>{event.preventDefault();action({action:'battery',pct:Number($('battery').value)});};
$('display').onerror=()=>message('Frame unavailable. Choose Restart to capture again.');
async function poll(){await refresh();setTimeout(poll,500);}availability();poll();
