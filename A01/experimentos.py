#!/usr/bin/env python3
"""Auxiliar de medicao; o simulador e inteiramente escrito em C."""
import csv
import hashlib
import pathlib
import platform
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent
REPETICOES = 20
subprocess.run(['make', '-C', str(ROOT)], check=True)
linhas = []

def registrar(texto):
    print(texto, flush=True)
    linhas.append(texto)

def executar(modo, n, pool, janela):
    p = subprocess.run([str(ROOT/'pix'), modo, str(n), str(pool), '0', str(janela), 'quiet'],
                       check=True, text=True, capture_output=True, timeout=30)
    return dict(item.split('=') for item in p.stdout.splitlines()[-1].split()[1:])

with (ROOT/'resultados.csv').open('w', newline='') as arq:
    campos = ['modo','N','pool','janela_ms','repeticao','esperado','obtido','perdido','ok']
    writer = csv.DictWriter(arq, fieldnames=campos)
    writer.writeheader()
    registrar('Ambiente: ' + platform.platform())
    registrar('SHA256 pix.c: ' + hashlib.sha256((ROOT/'pix.c').read_bytes()).hexdigest())
    registrar('20 repeticoes por configuracao; processamento=0 ms; valores em centavos.')
    registrar('modo,N,pool,janela_ms,falhas,repeticoes')
    for modo, pool, janela in [('sem',4,2), ('com',4,2), ('sem',1,2), ('sem',4,0)]:
        for n in [1,2,3,4,8,16,32,64]:
            falhas = 0
            for r in range(1, REPETICOES+1):
                d = executar(modo,n,pool,janela)
                falhas += d['ok'] == '0'
                writer.writerow({**{k:d[k] for k in ['modo','N','pool','esperado','obtido','perdido','ok']},
                                 'janela_ms':janela, 'repeticao':r})
            registrar(f'{modo},{n},{pool},{janela},{falhas},{REPETICOES}')

(ROOT/'resumo_experimentos.txt').write_text('\n'.join(linhas)+'\n')
