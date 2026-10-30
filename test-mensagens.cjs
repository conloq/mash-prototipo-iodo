// Teste sem navegador: node prototipo-iodo/test-mensagens.cjs
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = readFileSync(join(__dirname, 'web/maquina.html'), 'utf8');
const funcao = html.slice(html.indexOf('function mostrarResultado(msg)'), html.indexOf('function aoMensagem('));
const casos = [
  ['SEM_AMIDO;m=amarela;l=80;n=1', 'RESULTADO: SEM AMIDO', 'verde'],
  ['AMIDO_PRESENTE;m=escura;l=75;n=2', 'AMIDO PRESENTE', 'vermelho'],
  ['INCONCLUSIVO;m=luz;l=12;n=3', 'POUCA LUZ (12%)', 'amarelo'],
  ['INCONCLUSIVO;m=gota;l=90;n=4', 'NENHUMA GOTA', 'amarelo'],
  ['INCONCLUSIVO;m=cor;l=90;n=5', 'COR NAO RECONHECIDA', 'amarelo'],
  ['ERRO;opencv;n=6', 'CAMERA NAO PRONTA', 'amarelo'],
  ['INCONCLUSIVO;n=7', 'NAO FOI POSSIVEL LER', 'amarelo'],
];
for (const [mensagem, titulo, led] of casos) {
  const linhas = [];
  let acesos;
  const contexto = vm.createContext({ Date, linha: (n, texto) => {
    assert.ok(texto.length <= 20, `LCD cortaria: ${texto}`);
    linhas[n] = texto;
  }, leds: valor => { acesos = valor; }, bip() {} });
  vm.runInContext(funcao, contexto);
  contexto.mostrarResultado(mensagem);
  assert.equal(linhas[0], titulo);
  assert.equal(acesos[led], true);
}
console.log(`${casos.length} mensagens verificadas, sem corte no LCD.`);
