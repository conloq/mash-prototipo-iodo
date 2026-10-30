#!/usr/bin/env python3
"""Servidor local do protótipo do teste de iodo: funciona sem internet.

Serve as páginas de web/ por HTTPS e faz o papel do broker MQTT: a máquina
virtual (maquina.html) e a câmera (index.html) trocam mensagens por
/api/<codigo>/<canal>. Só usa a biblioteca padrão do Python.

Uso:
  python3 servidor.py --baixar   uma vez, COM internet: guarda OpenCV.js e mqtt.js em web/vendor/
  python3 servidor.py            HTTPS na porta 8443 (gera um certificado próprio na primeira vez)
  python3 servidor.py --http     sem HTTPS: a câmera do celular não liga, só a webcam em localhost

Rotas:
  GET  /api/ping                     o servidor está no ar (a página usa para escolher o modo local)
  GET  /api/<codigo>/eventos         fluxo de mensagens (Server-Sent Events)
  POST /api/<codigo>/<canal>         publica no canal; ?reter=1 guarda a última mensagem
"""

import argparse
import json
import queue
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

RAIZ = Path(__file__).resolve().parent
WEB = RAIZ / "web"
VENDOR = WEB / "vendor"
PASTA_CERT = RAIZ / ".cert"
DOWNLOADS = {
    "opencv.js": "https://cdn.jsdelivr.net/npm/@techstark/opencv-js@4.10.0-release.1/dist/opencv.js",
    "mqtt.min.js": "https://unpkg.com/mqtt@5/dist/mqtt.min.js",
}
CANAIS = {"pedido", "resultado", "status", "camera"}
TAMANHO_MAXIMO = 2000  # bytes por mensagem


class Central:
    """Distribui as mensagens entre as páginas inscritas no mesmo código."""

    def __init__(self):
        self.trava = threading.Lock()
        self.ouvintes = {}  # codigo -> conjunto de filas
        self.retidas = {}   # (codigo, canal) -> última mensagem retida

    def inscrever(self, codigo):
        fila = queue.Queue()
        with self.trava:
            self.ouvintes.setdefault(codigo, set()).add(fila)
            retidas = [(c, t) for (cod, c), t in self.retidas.items() if cod == codigo]
        for canal, texto in retidas:
            fila.put((canal, texto))
        return fila

    def sair(self, codigo, fila):
        with self.trava:
            self.ouvintes.get(codigo, set()).discard(fila)

    def publicar(self, codigo, canal, texto, reter):
        with self.trava:
            if reter:
                self.retidas[(codigo, canal)] = texto
            alvos = list(self.ouvintes.get(codigo, ()))
        for fila in alvos:
            fila.put((canal, texto))


CENTRAL = Central()


class Manipulador(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, formato, *args):
        pass  # só as mensagens da API aparecem no terminal

    def end_headers(self):
        # opencv.js tem 10 MB: guarda em cache; o resto sempre atualizado
        cache = "max-age=86400" if self.path.startswith("/vendor/") else "no-store"
        self.send_header("Cache-Control", cache)
        super().end_headers()

    def _partes(self):
        return [unquote(p) for p in urlparse(self.path).path.split("/") if p]

    def _json(self, status, corpo):
        dados = json.dumps(corpo).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(dados)))
        self.end_headers()
        self.wfile.write(dados)

    def do_GET(self):
        partes = self._partes()
        if partes[:1] != ["api"]:
            return super().do_GET()
        if partes == ["api", "ping"]:
            return self._json(200, {"ok": True})
        if len(partes) == 3 and partes[2] == "eventos":
            return self._eventos(partes[1])
        return self._json(404, {"error": "Rota não encontrada"})

    def do_POST(self):
        partes = self._partes()
        if not (len(partes) == 3 and partes[0] == "api" and partes[2] in CANAIS):
            return self._json(404, {"error": "Rota não encontrada"})
        tamanho = int(self.headers.get("Content-Length") or 0)
        if tamanho > TAMANHO_MAXIMO:
            return self._json(413, {"error": "Mensagem grande demais"})
        texto = self.rfile.read(tamanho).decode("utf-8", "replace")
        reter = parse_qs(urlparse(self.path).query).get("reter") == ["1"]
        codigo, canal = partes[1], partes[2]
        CENTRAL.publicar(codigo, canal, texto, reter)
        print(f"[{codigo}] {canal}: {texto}")
        return self._json(200, {"ok": True})

    def _eventos(self, codigo):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        fila = CENTRAL.inscrever(codigo)
        try:
            self.wfile.write(b": conectado\n\n")
            self.wfile.flush()
            while True:
                try:
                    canal, texto = fila.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")  # mantém a conexão viva
                    self.wfile.flush()
                    continue
                dados = "".join(f"data: {linha}\n" for linha in texto.split("\n"))
                self.wfile.write(f"event: {canal}\n{dados}\n".encode())
                self.wfile.flush()
        except OSError:
            pass  # a página fechou
        finally:
            CENTRAL.sair(codigo, fila)


class Servidor(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        pass  # navegador recusando o certificado ou fechando a conexão: não é erro do servidor


def ips_locais():
    ips = set()
    try:
        # Não envia nada: só descobre por qual interface sairia (funciona sem internet)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            ips.add(s.getsockname()[0])
    except OSError:
        pass
    try:
        ips.update(i[4][0] for i in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET))
    except OSError:
        pass
    return sorted(ip for ip in ips if not ip.startswith("127."))


def achar_openssl():
    candidatos = [
        shutil.which("openssl"),
        r"C:\Program Files\Git\mingw64\bin\openssl.exe",  # vem com o Git para Windows
        r"C:\Program Files\Git\usr\bin\openssl.exe",
    ]
    return next((c for c in candidatos if c and Path(c).exists()), None)


def garantir_certificado(ips):
    cert, chave, registro = PASTA_CERT / "cert.pem", PASTA_CERT / "chave.pem", PASTA_CERT / "ips.txt"
    anteriores = set(registro.read_text().split()) if registro.exists() else set()
    # Mudou de rede (outro IP)? Gera de novo, para o certificado valer no endereço novo.
    if cert.exists() and chave.exists() and set(ips) <= anteriores:
        return cert, chave
    openssl = achar_openssl()
    if not openssl:
        return None
    PASTA_CERT.mkdir(exist_ok=True)
    nomes = ",".join(["DNS:localhost", "IP:127.0.0.1"] + [f"IP:{ip}" for ip in ips])
    subprocess.run(
        [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-keyout", str(chave), "-out", str(cert), "-days", "825",
         "-subj", "/CN=mash-prototipo", "-addext", f"subjectAltName={nomes}"],
        check=True, capture_output=True,
    )
    registro.write_text(" ".join(ips))
    return cert, chave


def baixar():
    VENDOR.mkdir(parents=True, exist_ok=True)
    for nome, url in DOWNLOADS.items():
        print(f"Baixando {nome}...")
        urllib.request.urlretrieve(url, VENDOR / nome)
        print(f"  ok, {(VENDOR / nome).stat().st_size // 1024} KB")
    print("Pronto: as páginas passam a funcionar sem internet.")


def main():
    parser = argparse.ArgumentParser(description="Servidor local do protótipo do teste de iodo")
    parser.add_argument("--porta", type=int, default=8443)
    parser.add_argument("--http", action="store_true", help="sem HTTPS (a câmera do celular não liga)")
    parser.add_argument("--baixar", action="store_true", help="baixa OpenCV.js e mqtt.js para web/vendor/")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # mostra as mensagens na hora, mesmo redirecionado

    if args.baixar:
        return baixar()

    if not (VENDOR / "opencv.js").exists():
        print("Aviso: web/vendor/opencv.js não existe. Sem internet a câmera não carrega o OpenCV;")
        print("       rode 'python3 servidor.py --baixar' uma vez, com internet.\n")

    ips = ips_locais()
    servidor = Servidor(("0.0.0.0", args.porta), Manipulador)
    esquema = "http"
    if not args.http:
        par = garantir_certificado(ips)
        if par:
            contexto = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            contexto.load_cert_chain(*par)
            # o aperto de mão TLS acontece na thread de cada conexão, não trava o servidor
            servidor.socket = contexto.wrap_socket(servidor.socket, server_side=True,
                                                   do_handshake_on_connect=False)
            esquema = "https"
        else:
            print("Aviso: openssl não encontrado, servindo sem HTTPS (a câmera do celular não liga).\n")

    url = lambda host: f"{esquema}://{host}:{args.porta}"
    print("Servidor do protótipo no ar. Ctrl+C para parar.\n")
    print(f"  Máquina (notebook/projetor): {url('localhost')}/maquina.html")
    for ip in ips:
        print(f"  Câmera (celular, mesma rede): {url(ip)}/")
    if esquema == "https":
        print("\n  O certificado é próprio: o navegador avisa uma vez; use 'Avançado' e 'Continuar'")
        print("  (no iPhone: 'Mostrar detalhes' e 'visitar este site').")
    if ips:
        print(f"\n  O celular não abre? Teste {url(ips[0])}/api/ping no celular. Se não carregar,")
        print(f"  é o firewall do notebook bloqueando a porta {args.porta}:")
        print(f"    Linux:   sudo ufw allow {args.porta}/tcp")
        print(f"    Windows: marque a rede como 'Privada' e permita o Python no aviso do firewall, ou rode")
        print(f'             netsh advfirewall firewall add rule name="MASH prototipo" dir=in action=allow protocol=TCP localport={args.porta}')
    print()
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nParado.")


if __name__ == "__main__":
    main()
