# Como o protótipo funciona

## Objetivo e limites

Demonstrar uma máquina compacta dedicada ao teste de iodo: botão, captura de imagem, regra de cor e resultado no LCD. Na versão local, a máquina é uma página web e a câmera é a do celular. Não há processamento em ESP32, API do MASH, banco, histórico persistente ou controle de aquecimento nesse fluxo.

O resultado é uma classificação visual experimental. Um líquido amarelo ou azul-escuro pode representar uma amostra na apresentação, mas sua cor não demonstra composição química. O protótipo não determina segurança de alimentos nem substitui um teste químico validado.

## Sequência de uma leitura

1. Inicie `python3 servidor.py` no notebook e mantenha o terminal aberto.
2. Abra `/maquina.html` no notebook e `/` no celular, nos endereços impressos. Use o mesmo código de máquina e modo nas duas páginas.
3. Ligue a câmera e espere o OpenCV iniciar. Posicione a amostra no círculo central e deixe o anel tracejado sobre fundo branco.
4. Aperte ANALISAR na máquina. Ela acende o indicador branco, apita e publica um número no canal `pedido`.
5. A câmera espera 400 ms e captura um quadro do vídeo. O processamento é feito no navegador do celular, sem enviar a foto ao servidor.
6. A câmera publica a classe, o motivo e medidas numéricas no canal `resultado`.
7. A máquina mostra o resultado e apaga o indicador branco. Após 12 segundos volta ao estado inicial. Sem resposta em 15 segundos, avisa que a câmera não respondeu.

O botão Analisar da página da câmera também funciona, mas a máquina só mostra resultados enquanto está aguardando uma leitura. Use uma máquina e uma câmera por código; não abra várias sessões com o mesmo código.

## Arquivos e funções

| Arquivo | Responsabilidade |
|---|---|
| `web/index.html` | `ligarCamera()` pede acesso à câmera; `medir()` processa a imagem; `analisar()` publica a resposta; `mostrar()` apresenta o diagnóstico no celular |
| `web/maquina.html` | `pedirAnalise()` inicia o pedido; `mostrarResultado()` interpreta a resposta; `linha()` limita o texto a 20 caracteres; `leds()` e `bip()` representam as saídas |
| `web/comum.js` | `escolherModo()` detecta o servidor local; `conectarCanal()` seleciona SSE/HTTP ou MQTT; `carregarScript()` tenta os arquivos locais antes da CDN |
| `servidor.py` | Serve `web/` por HTTPS e distribui mensagens com `Central`; gera certificado próprio via OpenSSL e baixa dependências com `--baixar` |
| `test-mensagens.cjs` | Verifica sete respostas da máquina virtual, LEDs esperados e textos sem corte no LCD |
| `wokwi/` | Alternativa de circuito ESP32 simulado, opcional e não necessária para o fluxo local |

## Análise da imagem

`medir()` usa OpenCV.js para converter RGBA em RGB e suavizar ruído. Duas máscaras delimitam o círculo da amostra e o anel do fundo. A média de cor do anel serve de referência de branco.

A segmentação compara cada pixel do círculo com a referência (`absdiff`). Mantém diferenças acima do limite configurado, combina a máscara com o círculo e aplica abertura morfológica para remover pontos pequenos. A cor média é calculada apenas nessa mancha, evitando misturá-la com o branco ao redor.

A cor da mancha é normalizada por canal em relação ao fundo, limitada a 255, e convertida para HSV. A ordem das regras é:

1. Referência escura: inconclusivo, motivo `luz`.
2. Área da mancha insuficiente: inconclusivo, motivo `gota`.
3. Brilho relativo baixo: `AMIDO_PRESENTE`, motivo `escura`.
4. Azul dentro da faixa e com saturação suficiente: `AMIDO_PRESENTE`, motivo `azul`.
5. Amarelo/âmbar dentro da faixa e com saturação suficiente: `SEM_AMIDO`, motivo `amarela`.
6. Fora dessas condições: inconclusivo, motivo `cor`.

Não há aprendizado de máquina nem IA generativa nessa decisão. Os limites estão nos campos de calibração da página e ficam no `localStorage` do navegador. Não são ajustados automaticamente. Uma nova câmera ou iluminação pode exigir nova conferência; o anel precisa continuar sobre branco.

## Luz e indicadores

- **Branco, Luz:** acende durante a espera da captura. É apenas uma representação da futura iluminação interna; não aciona a lanterna do celular.
- **Verde:** cor reconhecida como representação de ausência de amido.
- **Vermelho:** cor reconhecida como representação de presença de amido.
- **Amarelo:** leitura inconclusiva ou falha técnica; consulte a mensagem do LCD.

`Luz: OK` indica que o brilho médio do fundo passou pelo limite da regra, não uma medição física de iluminação. O percentual é a média RGB do fundo dividida por 255, vezes 100. Não é lux; um fundo incorreto ou reflexos podem enganar essa verificação.

## Comunicação local e online

Sem internet, execute antes `python3 servidor.py --baixar` com internet para guardar o OpenCV. Na apresentação, o servidor não precisa de internet. Celular e notebook precisam de ligação local por Wi-Fi/hotspot; Wi-Fi aqui é transporte entre aparelhos, não acesso à internet. Com webcam no notebook, nem essa rede é necessária.

No modo local, `POST /api/<codigo>/<canal>` publica mensagens e `GET /api/<codigo>/eventos` as entrega por Server-Sent Events. O servidor guarda apenas mensagens retidas e conexões em memória; não salva as imagens ou um histórico de análises. Ao reiniciar, perde o estado.

No modo online, as mesmas mensagens passam por MQTT no broker público HiveMQ. O código de máquina separa os tópicos, mas não é senha nem autenticação.

Exemplo de mensagem:

```text
AMIDO_PRESENTE;h=110;s=180;v=0.30;m=azul;l=82;n=4
```

`h` é matiz OpenCV (0–180), `s` é saturação (0–255), `v` é brilho normalizado (0–1), `m` é motivo, `l` é brilho estimado do fundo em percentual e `n` é número do pedido. O exemplo ilustra o formato, não uma medição real. O receptor atual não valida a correspondência de `n`; por isso não use sessões concorrentes no mesmo código.

## Preparação e segurança

- Requisitos: Python 3, navegador com câmera e OpenSSL para gerar o certificado HTTPS. Node.js só é necessário para o teste automatizado.
- O certificado e sua chave ficam em `.cert/`, fora do Git. Dependências baixadas ficam em `web/vendor/`, também fora do Git. Execute `--baixar` em cada cópia antes de usar offline.
- HTTPS com certificado próprio pode exigir configuração de confiança no celular; aceitar um aviso não garante permissão de câmera em todo navegador. Valide no aparelho que será usado. A webcam em `localhost` é a alternativa.
- O servidor escuta em todas as interfaces e não tem autenticação. Use somente rede de demonstração confiável, não o exponha à internet e encerre-o após o uso. Não envie dados pessoais pelo broker público.
- Se o celular não abrir, confira primeiro servidor ativo, IP atual, porta 8443, certificado e isolamento entre clientes da rede. Firewall é uma hipótese, não diagnóstico confirmado só pelo erro de conexão.
- Resultado inconclusivo não significa ausência de amido. O método não foi validado com mostura real, variação de reagentes ou condições controladas.

## Verificação disponível

```bash
node test-mensagens.cjs
python3 -m py_compile servidor.py
```

O teste de mensagens não verifica a câmera ou a classificação. A experiência manual relatada pelo autor não substitui validação experimental. O circuito Wokwi não foi compilado nesta preparação do repositório.
