const $=id=>document.getElementById(id);
const fmt=(n,d=1)=>Number.isFinite(n)?n.toFixed(d):'—';
let busy=false;
function showTelemetry(reading){
  $('telemetry-device').textContent=reading?.device_id??'—';
  $('telemetry-time').textContent=reading?new Date(reading.ts*1000).toLocaleString():'—';
  $('telemetry-boot').textContent=reading?.boot_id??'—';
  $('telemetry-seq').textContent=reading?.seq??'—';
  $('telemetry-state').textContent=!reading?'Awaiting data':
    (reading.simulated?'SIMULATED':'ESP32 hardware')+' · '+(Date.now()/1000-reading.ts<15?'Recent message':'Stale message');
  const body=$('telemetry-values');body.replaceChildren();
  if(!reading){
    const row=document.createElement('tr'),cell=document.createElement('td');
    cell.colSpan=3;cell.textContent='No message received for this meter.';row.append(cell);body.append(row);
    $('telemetry-payload').textContent='No message received yet.';return;
  }
  const fields=[['Voltage (RMS)','voltage_v','V',4],['Current A (RMS)','current_a','A',6],
    ['Active power','power_w','W',4],['Power factor','power_factor','Unitless',5],['Line frequency','frequency_hz','Hz',4]];
  for(const [label,key,unit,digits] of fields){
    const row=document.createElement('tr');
    for(const value of [label,fmt(reading[key],digits),unit]){
      const cell=document.createElement('td');cell.textContent=value;row.append(cell);
    }body.append(row);
  }
  const payload={device_id:reading.device_id,boot_id:reading.boot_id,seq:reading.seq};
  for(const [,key] of fields)payload[key]=reading[key];
  payload.simulated=Boolean(reading.simulated);
  $('telemetry-payload').textContent=JSON.stringify(payload,null,2);
}
function chart(rows){
  const svg=$('chart');svg.replaceChildren();
  const add=(tag,attrs,text)=>{const e=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,v] of Object.entries(attrs))e.setAttribute(k,v);if(text)e.textContent=text;svg.append(e);};
  if(!rows.length){add('text',{x:500,y:130,'text-anchor':'middle',fill:'#809080','font-size':15},'Waiting for power readings');return;}
  const min=Math.min(0,...rows.map(r=>r.power_w)),max=Math.max(1,...rows.map(r=>r.power_w));
  const t0=rows[0].ts,t1=rows.at(-1).ts;
  const x=r=>55+(r.ts-t0)/Math.max(1,t1-t0)*930,y=v=>225-(v-min)/(max-min)*200;
  for(let i=0;i<=4;i++){const v=min+(max-min)*i/4;add('line',{x1:55,x2:985,y1:y(v),y2:y(v),stroke:'#e8ede5'});add('text',{x:45,y:y(v)+4,'text-anchor':'end',fill:'#809080','font-size':11},fmt(v,0));}
  let path='';rows.forEach((r,i)=>{const prev=rows[i-1];path+=(i===0||r.ts-prev.ts>Math.max(10,(t1-t0)/600*2)?'M':'L')+x(r)+','+y(r.power_w)+' ';});
  add('path',{d:path,fill:'none',stroke:'#739b45','stroke-width':3,'stroke-linejoin':'round'});
  if(rows.length===1)add('circle',{cx:x(rows[0]),cy:y(rows[0].power_w),r:4,fill:'#739b45'});
  $('start').textContent=new Date(t0*1000).toLocaleString();$('end').textContent=new Date(t1*1000).toLocaleString();
}
async function get(url){const res=await fetch(url);if(!res.ok)throw Error('Server returned '+res.status);return res.json();}
async function refresh(){
  if(busy)return;busy=true;
  try{
    const devices=await get('/api/devices');const chosen=$('device').value;
    $('device').replaceChildren(...devices.map(d=>{const o=document.createElement('option');o.value=d.device_id;o.textContent=d.device_id+(d.simulated?' · DEMO':'');return o;}));
    if(devices.some(d=>d.device_id===chosen))$('device').value=chosen;
    const device=$('device').value,hours=$('hours').value;
    showTelemetry(devices.find(d=>d.device_id===device));
    const query=new URLSearchParams({device,hours});$('export').href='/api/export?'+query;
    const data=await get('/api/readings?'+query),r=data.latest,s=data.summary;
    const online=r&&Date.now()/1000-r.ts<15;
    $('status').textContent=online?'● Receiving data':r?'○ Meter stale':'○ Awaiting meter';
    $('notice').textContent=r?(r.simulated?'SIMULATED DATA · This meter is a software demonstration. ':'LIVE HARDWARE · ')+(online?'Updating every 3 seconds.':'No recent readings. Check device, Wi-Fi, and MQTT broker.'):'No telemetry in this period. Run python server/app.py --demo to explore the dashboard.';
    $('power').textContent=fmt(r?.power_w);$('voltage').textContent=fmt(r?.voltage_v);$('current').textContent=fmt(r?.current_a,3);
    $('energy').textContent=s.samples?fmt(s.energy_kwh,4):'—';$('pf').textContent=fmt(r?.power_factor,3);
    $('freq').textContent=fmt(r?.frequency_hz,2)+' Hz';$('peak').textContent=fmt(s.peak_w)+' W';
    $('coverage').textContent=fmt(s.covered_seconds/60)+' min';
    $('updated').textContent=r?'Last sample · '+new Date(r.ts*1000).toLocaleString():'No readings yet';chart(data.points);
  }catch(e){$('status').textContent='○ Server unavailable';$('telemetry-state').textContent='Refresh failed · Displayed message may be stale';$('notice').textContent='Unable to refresh: '+e.message+' · Displayed values may be stale.';}
  finally{busy=false;}
}
$('device').addEventListener('change',refresh);$('hours').addEventListener('change',refresh);refresh();setInterval(refresh,3000);
