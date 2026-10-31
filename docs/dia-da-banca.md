# Dia da banca: passo a passo

Siga na ordem. Cada bloco termina com uma conferência: se ela falhar, não avance, vá para [Se algo falhar](#se-algo-falhar).

O que o protótipo mostra e o que não mostra está em [Como o protótipo funciona](funcionamento.md). Na fala, apresente as amostras como **representações** dos resultados, não como medição química de amido.

## 1. Em casa, com internet (de véspera)

1. Leve a pasta **inteira** do protótipo no notebook da banca, incluindo `web/vendor/`. Essa pasta não vai para o Git: ou você copia a pasta pronta, ou roda o comando abaixo no próprio notebook.
2. Baixe o OpenCV e o mqtt.js, uma única vez:
   ```bash
   python3 servidor.py --baixar        # Windows: python servidor.py --baixar
   ```
3. **Confira:** existem `web/vendor/opencv.js` (cerca de 10 MB) e `web/vendor/mqtt.min.js`. Sem eles, a câmera não carrega o OpenCV sem internet.
4. Confira as ferramentas: `python3 --version` e `openssl version` respondem. O OpenSSL gera o certificado HTTPS; no Windows ele vem com o Git.
5. Faça o **ensaio completo** (blocos 2 a 4) pelo menos uma vez, com o celular que será usado.
6. Grave um **vídeo curto** da demonstração funcionando, para o caso de tudo falhar (bloco 6).

### O que levar

- Notebook com carregador e o cabo do projetor (HDMI ou adaptador).
- Celular carregado e um **apoio fixo** para ele (tripé, suporte, uma pilha de livros). O celular não pode se mexer durante a leitura.
- Um **prato ou azulejo branco**, liso e sem brilho forte.
- As duas amostras: uma **azul-escura** (representa "amido presente") e uma **amarela** (representa "sem amido"), em recipientes pequenos e identificados. Se usar reagentes de verdade, é por conta de vocês: este projeto não traz protocolo. Não ingerir as amostras.
- Conta-gotas ou colher para pingar, papel toalha, um pano.
- Se possível, um roteador sem internet. Se não, o hotspot do celular.

## 2. Na banca: montar (chegue 20 minutos antes)

### 2.1 Rede

1. Conecte o **notebook e o celular na mesma rede**: um roteador sem internet ou o hotspot de um dos aparelhos. Nos testes usamos o hotspot do iPhone (faixa `172.20.10.x`).
2. Se vocês dependerem de **zero internet**, teste uma vez com os dados móveis desligados. Se o hotspot não ligar assim, use um roteador ou a webcam do notebook (bloco 6).

### 2.2 Firewall do notebook

O firewall costuma descartar em silêncio as conexões do celular. Libere a porta 8443:

- **Linux:** `sudo ufw allow 8443/tcp`
- **Windows** (terminal como administrador):
  `netsh advfirewall firewall add rule name="MASH prototipo" dir=in action=allow protocol=TCP localport=8443`
- Ao terminar, remova a regra: `sudo ufw delete allow 8443/tcp`, ou no Windows `netsh advfirewall firewall delete rule name="MASH prototipo"`.

### 2.3 Servidor

1. Na pasta do protótipo: `python3 servidor.py`.
2. O terminal mostra os endereços. Anote o IP da **câmera**, por exemplo `https://172.20.10.2:8443/`. O IP muda a cada rede; o certificado é refeito sozinho quando isso acontece.
3. **Deixe esse terminal aberto** até o fim. Cada mensagem trocada aparece nele.

### 2.4 Celular

1. Abra primeiro `https://<IP>:8443/api/ping`. O navegador avisa do certificado: no iPhone, "Mostrar detalhes" e "visitar este site"; no Android, "Avançado" e "Continuar".
2. **Confira:** aparece `{"ok": true}`. Se não, vá para [Se algo falhar](#se-algo-falhar).
3. Abra `https://<IP>:8443/`, toque em **Ligar câmera** e permita o acesso.
4. Espere a mensagem "OpenCV pronto" ou "Pronto para analisar".

### 2.5 Notebook (projetor)

1. Abra `https://localhost:8443/maquina.html` e coloque em tela cheia (F11).
2. **Confira os dois lados:**
   - no LCD da máquina, a última linha mostra `Rede:OK Camera:OK`;
   - no celular, em "Conexão", aparece "Modo: local (sem internet)", "conectado" e "Máquina: online".
3. O código da máquina é o mesmo nas duas páginas (`demo01`). Se mudar um, mude o outro.

## 3. Calibrar com as amostras do local

A luz da sala muda os números, então calibre **depois** de montar o prato e o celular no lugar definitivo. Os ajustes ficam salvos no navegador daquele celular, **por endereço**: se o IP mudar, a calibração começa de novo. Calibre no celular que vai usar, já na rede definitiva.

1. No celular, abra "Calibração" e marque **Leitura contínua**. Nesse modo a página só mostra a leitura, sem enviar nada para a máquina.
2. Pingue a amostra **azul-escura** no prato e posicione dentro do círculo pequeno da tela. O anel tracejado em volta tem que ficar sobre o branco do prato.
3. **Esperado:** "AMIDO PRESENTE". Repita com a amostra **amarela**: "SEM AMIDO".
4. Se a leitura sair errada, olhe o motivo mostrado e ajuste o campo correspondente (valores iniciais entre parênteses):

   | Motivo mostrado | O que fazer |
   |---|---|
   | pouca luz ou anel fora do branco | melhorar a luz, sair da sombra, conferir se o anel está sobre o branco; ou baixar "Luz mínima no prato" (70) |
   | nenhuma gota no círculo | centralizar a amostra; baixar "Diferença mínima do fundo" (35) ou "Área mínima da gota" (3) |
   | cor fora das faixas | comparar o `H` mostrado com as faixas de matiz: azul 90 a 140, amarelo/âmbar 8 a 40; ajustar a faixa ou a "Saturação mínima" (50) |
   | gota escura, mas a amostra é amarela | o brilho relativo ficou abaixo de "Escuro se brilho relativo" (0,40): baixe esse valor ou use uma amostra amarela mais clara |

5. Desmarque **Leitura contínua** e recolha as amostras.

## 4. Demonstração

1. Projete a máquina: "esta é a máquina do teste de iodo".
2. Posicione a amostra **azul-escura** no círculo, com o celular parado.
3. Aperte **ANALISAR** na máquina (ou a tecla espaço). A luz branca acende, a tela mostra "Analisando..." e o LCD responde em poucos segundos.
4. **Esperado:** LED vermelho, "AMIDO PRESENTE". Troque pela amostra **amarela** e repita: LED verde, "SEM AMIDO".
5. Mostre o terminal do servidor, se perguntarem como as duas partes conversam: cada pedido e cada resultado aparece lá.
6. Fechamento sugerido: "isto é uma prova de conceito; a câmera embarcada e a integração com o sistema são a próxima etapa".

Não troque a amostra enquanto a máquina estiver em "Analisando...". Se a leitura sair inconclusiva, a própria tela diz o motivo: reposicione e aperte de novo. Inconclusivo não quer dizer "sem amido".

## 5. Ao terminar

1. `Ctrl+C` no terminal do servidor.
2. Remova a regra de firewall (bloco 2.2).
3. O servidor não tem senha: não deixe rodando na rede da faculdade.

## Se algo falhar

| O que aparece | Causa provável | O que fazer |
|---|---|---|
| Celular não abre `/api/ping` e fica carregando | firewall do notebook, IP errado ou aparelhos em redes diferentes | refazer 2.2; conferir o IP impresso no terminal; ligar os dois na mesma rede |
| Celular abre, mas o IP mudou depois | trocou de rede | parar o servidor e rodar de novo; usar o IP novo |
| "Câmera bloqueada: abra por HTTPS e permita o acesso" | certificado não aceito ou permissão negada | repetir 2.4; conferir a permissão da câmera nos ajustes do navegador; se não resolver, bloco 6 |
| "OpenCV não carregou" | `web/vendor/opencv.js` não existe | com internet, `python3 servidor.py --baixar` |
| LCD: `Camera:--` | a página da câmera não está aberta ou usa outro código | abrir a câmera; igualar o código nas duas páginas |
| LCD: "Sem conexao" | a máquina perdeu o servidor | conferir se o terminal do servidor ainda está rodando; clicar em Conectar |
| LCD: "Camera nao respondeu" | câmera desligada, OpenCV ainda carregando ou celular em segundo plano | deixar o celular com a página aberta e a tela ligada; tentar de novo |
| LCD: "CAMERA NAO PRONTA" | OpenCV carregando ou câmera desligada | esperar "Pronto para analisar" no celular |
| LCD: "POUCA LUZ" | prato escuro, em sombra, ou anel fora do branco | ver o bloco 3 |
| LCD: "NENHUMA GOTA" | amostra fora do círculo ou muito parecida com o prato | centralizar; ver o bloco 3 |
| LCD: "COR NAO RECONHECIDA" | cor fora das faixas de azul/escuro e amarelo/âmbar | recalibrar; usar as amostras combinadas |
| Resultado errado de forma repetida | luz mudou depois da calibração | recalibrar (bloco 3) |

## 6. Plano B

1. **Webcam do notebook, sem celular e sem rede.** No notebook, rode `python3 servidor.py --http`. Abra, em duas janelas lado a lado (não em abas), `http://localhost:8443/maquina.html` e `http://localhost:8443/`. Na segunda, ligue a câmera apontada para o prato. Funciona porque `localhost` é aceito pelo navegador sem HTTPS.
2. **Só a câmera, sem a máquina.** O botão **Analisar** da própria página da câmera funciona sozinho e mostra o resultado e as medidas na tela do celular.
3. **Vídeo gravado no ensaio.** Se nada funcionar, mostre o vídeo e diga que é a gravação do protótipo.
