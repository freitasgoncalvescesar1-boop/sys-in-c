# Exemplo pythont 5.0 - Arquivos (I/O), Metodos de String, POO & Multi-Assignment

print("=== Testando pythont 5.0 (Arquivos & Metodos de String) ===")

# 1. Metodos de String
nome = "cesar freitas"
print(f"Original: {nome}")
print(f"Maiusculo (.upper()): {nome.upper()}")
print(f"Substituicao (.replace()): {nome.replace('cesar', 'imperador')}")

# 2. Multiplas Atribuicoes
x, y = 100, 200
print(f"Atribuicao multipla: x={x}, y={y}")

# 3. Gravacao e Leitura de Arquivo
f = open("py_saida.txt", "w")
f.write("Gravado nativamente pelo pythont 5.0!\n")
f.write("Segunda linha gravada no arquivo pelo runtime C.\n")
f.close()

f2 = open("py_saida.txt", "r")
conteudo = f2.read()
f2.close()

print("Conteudo lido do arquivo:")
print(conteudo)

# 4. Classes (POO)
class Monstro:
    def __init__(self, especie, vida):
        self.nome = especie
        self.vida = vida
    def curar(self, v):
        self.vida += v

m = Monstro("Dragao Vermelho", 500)
m.curar(150)
print(f"Monstro: {m.nome} | Vida Total: {m.vida}")

print("Finalizado com sucesso no pythont 5.0!")
