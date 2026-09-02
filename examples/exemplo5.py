# Exemplo pythont 4.0 - Classes, Metodos (POO) & Dicionarios com Aspas Simples

print("=== Testando pythont 4.0 (POO, Classes & Dicionarios) ===")

class Player:
    def __init__(self, nome, vida, forca):
        self.nome = nome
        self.vida = vida
        self.forca = forca

    def atacar(self, dano):
        self.vida -= dano

    def curar(self, cura):
        self.vida += cura

p1 = Player('Cesar', 100, 35)
print(f"Player criado: {p1.nome} | Vida: {p1.vida} | Forca: {p1.forca}")

p1.atacar(30)
print(f"Apos sofrer ataque de 30: {p1.vida} de vida restante")

p1.curar(50)
print(f"Apos se curar em 50: {p1.vida} de vida atualizada")

# Dicionario com aspas simples
config = {
    'modo': 'Survival',
    'fase': 7,
    'servidor': 'BR-SaoPaulo'
}

print(f"Servidor: {config['servidor']} | Modo: {config['modo']} | Fase: {config['fase']}")
print("Finalizado com sucesso no pythont 4.0!")
