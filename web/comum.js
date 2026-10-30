// Código compartilhado entre a câmera (index.html) e a máquina virtual (maquina.html).
//
// As duas páginas conversam por "canais" de um mesmo código de máquina:
//   pedido    máquina → câmera   "faça uma leitura" (número do pedido)
//   resultado câmera → máquina   "CLASSE;h=..;s=..;v=..;n=.."
//   status    máquina            "online" / "offline" (retido)
//   camera    câmera             "online" / "offline" (retido)
//
// Dois transportes, com as mesmas mensagens:
//   local   servidor.py no notebook; funciona sem internet
//   online  broker MQTT público (HiveMQ); é o que o ESP32 do Wokwi usa

const BROKER_ONLINE = 'wss://broker.hivemq.com:8884/mqtt';
const PREFIXO_MQTT = 'conloq-mash-proto';
const CANAIS = ['pedido', 'resultado', 'status', 'camera'];

const URL_OPENCV = [
  'vendor/opencv.js',
  'https://cdn.jsdelivr.net/npm/@techstark/opencv-js@4.10.0-release.1/dist/opencv.js',
];
const URL_MQTT = ['vendor/mqtt.min.js', 'https://unpkg.com/mqtt@5/dist/mqtt.min.js'];

// Carrega um script tentando cada endereço em ordem: a cópia local
// (baixada por "servidor.py --baixar") primeiro, a CDN depois.
function carregarScript(urls) {
  return new Promise((ok, falha) => {
    const tentar = i => {
      if (i >= urls.length) return falha(new Error('Não carregou: ' + urls.join(', ')));
      const s = document.createElement('script');
      s.src = urls[i];
      s.async = true;
      s.onload = () => ok(urls[i]);
      s.onerror = () => { s.remove(); tentar(i + 1); };
      document.head.appendChild(s);
    };
    tentar(0);
  });
}

async function servidorLocalDisponivel() {
  try {
    const r = await fetch('api/ping', { cache: 'no-store' });
    return r.ok;
  } catch {
    return false;
  }
}

// "auto": usa o servidor local se a página veio dele; senão, o broker online.
async function escolherModo(preferencia) {
  if (preferencia === 'local' || preferencia === 'online') return preferencia;
  return (await servidorLocalDisponivel()) ? 'local' : 'online';
}

// Conecta no canal da máquina e devolve { publicar(canal, texto, reter), fechar() }.
// ultimaVontade: mensagem retida enviada quando a página fecha ou cai.
async function conectarCanal({ modo, codigo, aoMensagem, aoEstado, ultimaVontade }) {
  return modo === 'local'
    ? conectarLocal({ codigo, aoMensagem, aoEstado, ultimaVontade })
    : conectarOnline({ codigo, aoMensagem, aoEstado, ultimaVontade });
}

function conectarLocal({ codigo, aoMensagem, aoEstado, ultimaVontade }) {
  const base = `api/${encodeURIComponent(codigo)}/`;
  const fonte = new EventSource(base + 'eventos'); // reconecta sozinho se cair
  fonte.onopen = () => aoEstado(true);
  fonte.onerror = () => aoEstado(false);
  CANAIS.forEach(c => fonte.addEventListener(c, e => aoMensagem(c, e.data)));

  const endereco = (canal, reter) => base + canal + (reter ? '?reter=1' : '');
  const publicar = (canal, texto, reter = false) =>
    fetch(endereco(canal, reter), { method: 'POST', body: texto }).catch(() => {});
  const despedida = () => {
    if (ultimaVontade) navigator.sendBeacon(endereco(ultimaVontade.canal, true), ultimaVontade.texto);
  };
  addEventListener('pagehide', despedida);

  return {
    publicar,
    fechar: () => { despedida(); removeEventListener('pagehide', despedida); fonte.close(); },
  };
}

async function conectarOnline({ codigo, aoMensagem, aoEstado, ultimaVontade }) {
  if (!window.mqtt) await carregarScript(URL_MQTT);
  const base = `${PREFIXO_MQTT}/${codigo}/`;
  const opcoes = {
    clientId: 'mash-' + Math.random().toString(16).slice(2, 10),
    clean: true,
    reconnectPeriod: 3000,
  };
  if (ultimaVontade) {
    opcoes.will = { topic: base + ultimaVontade.canal, payload: ultimaVontade.texto, retain: true, qos: 0 };
  }
  const cliente = mqtt.connect(BROKER_ONLINE, opcoes);
  cliente.on('connect', () => { aoEstado(true); cliente.subscribe(base + '+'); });
  cliente.on('close', () => aoEstado(false));
  cliente.on('message', (topico, msg) => {
    const canal = topico.slice(base.length);
    if (CANAIS.includes(canal)) aoMensagem(canal, msg.toString());
  });

  return {
    publicar: (canal, texto, reter = false) => cliente.publish(base + canal, texto, { retain: reter }),
    fechar: () => {
      if (ultimaVontade) cliente.publish(base + ultimaVontade.canal, ultimaVontade.texto, { retain: true });
      cliente.end();
    },
  };
}
