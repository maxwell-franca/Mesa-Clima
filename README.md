# 🌤️ MesaClima

**Acessório de mesa com ESP32 e display OLED: mostra a hora e o clima da cidade onde está, e se configura sozinho pelo celular, sem precisar mexer no código.**

![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino-00878F?style=for-the-badge&logo=arduino&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Licença](https://img.shields.io/badge/Licen%C3%A7a-MIT-green?style=for-the-badge)

---

## 🧠 O que é

Um protótipo embarcado que liga, conecta no Wi-Fi salvo e passa a exibir hora, tempo atual, detalhes e previsão, tudo em um display de 128x64 pixels:

- 📶 **Configuração pelo celular:** na primeira vez, o aparelho cria a rede `MesaClima-XXXX` e abre uma página de configuração (captive portal) para escolher o Wi-Fi e, se quiser, a cidade.
- 📍 **Localização automática:** a cidade é estimada pelo IP ([ipwho.is](https://ipwho.is/)), ou você digita a cidade no portal.
- 🌦️ **Clima e fuso:** dados do [Open-Meteo](https://open-meteo.com/), gratuito para uso **não comercial**, sem chave de API. O fuso horário também vem de lá, então o relógio acerta sozinho em qualquer cidade.
- 🕒 **Hora:** sincronizada por NTP.

---

## ✨ Funcionalidades

Quatro telas que se alternam a cada 5 segundos:

| Tela | Conteúdo |
| --- | --- |
| Relógio | Hora com dois pontos piscando, dia da semana e data |
| Agora | Ícone do tempo, temperatura grande e descrição |
| Detalhes | Sensação térmica, umidade, vento, pressão, chance de chuva e próximo nascer ou pôr do sol |
| Previsão | Hoje e os próximos 3 dias, com ícone, máxima e mínima |

Mais:

- Cidade e temperatura no cabeçalho, com aviso `offline` quando os dados têm mais de 1 hora
- Tela escurecida das 23h às 6h para poupar o OLED
- Clima atualizado a cada 15 minutos, nova tentativa em 1 minuto após falha
- Reconexão automática do Wi-Fi a cada 30 segundos; sem rede por 10 minutos, o portal de configuração abre sozinho
- Compatível com displays **SSD1306 (0,96")** e **SH1106 (1,3")** (opção `USE_SH1106` no código)
- Fontes com suporte a acentos, para nomes de cidades e textos em português
- Nenhuma senha ou dado pessoal no código

---

## 🎮 Botão BOOT (GPIO 0)

| Ação | O que faz |
| --- | --- |
| Toque curto | Próxima tela |
| Segurar **3 s** | Abre o portal de configuração (Wi-Fi e cidade) |
| Segurar **10 s** | **Apaga tudo** (reset de fábrica): Wi-Fi salvo, cidade e demais ajustes |

O display avisa o que vai acontecer ao soltar o botão.

---

## 🔧 Hardware

| Componente | Observação |
| --- | --- |
| ESP32 | Placa de desenvolvimento ("ESP32 Dev Module") |
| Display OLED | 128x64, I2C, SSD1306 ou SH1106 |
| Cabo USB | Para gravar e alimentar |
| Rede Wi-Fi 2.4 GHz | O ESP32 não conecta em redes 5 GHz |

### Ligação do display

O código usa o I2C padrão do ESP32:

| OLED | ESP32 |
| --- | --- |
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3V3 |
| GND | GND |

---

## 📁 Estrutura

```
mesa-clima/
├── MesaClima/
│   └── MesaClima.ino   # código principal
├── COMMITS.md          # padrão de commits do projeto
├── LICENSE
└── README.md
```

A pasta do sketch precisa ter o mesmo nome do arquivo `.ino` para a Arduino IDE abri-lo.

---

## 🚀 Como usar

1. **Clone o repositório**
   ```
   git clone https://github.com/maxwell-franca/mesa-clima.git
   ```
2. **Instale as bibliotecas** pelo Gerenciador de Bibliotecas da Arduino IDE:
   - U8g2 (olikraus)
   - WiFiManager (tzapu, versão **2.0.17**)
   - ArduinoJson (versão **7.x**)
3. **Escolha o seu display** no começo do `MesaClima.ino`: `USE_SH1106` em `0` para SSD1306 (0,96") ou `1` para SH1106 (1,3").
4. **Abra o `MesaClima.ino`**, selecione a placa **ESP32 Dev Module** e grave no ESP32.
5. **Configure pelo celular:** o display mostra o nome da rede `MesaClima-XXXX`. Conecte o celular nela, e a página abre sozinha (ou acesse `192.168.4.1`). Escolha o seu Wi-Fi, digite a senha e, se quiser, a cidade.
6. **Pronto:** o aparelho reinicia, conecta e começa a mostrar hora e clima.

Para trocar de rede ou de cidade depois, segure o botão BOOT por 3 segundos.

---

## 🔒 Privacidade e segurança

O projeto não usa chaves de API nem contas. O que sai do aparelho:

| Para onde | O que vai | Quando |
| --- | --- | --- |
| ipwho.is | Seu endereço IP (devolve a cidade estimada) | No modo automático, uma vez por hora (ou se a cidade digitada não for encontrada) |
| Open-Meteo, geocodificação | O nome da cidade digitada | Só no modo manual, uma vez por cidade (o resultado fica guardado) |
| Open-Meteo, previsão | Coordenadas da cidade e o seu endereço IP | A cada 15 minutos |
| Servidores NTP (`pool.ntp.org`, `a.ntp.br`, `time.google.com`) | Consulta de hora, com o seu endereço IP | Ao conectar e periodicamente |

As coordenadas enviadas são as da **cidade**, vindas da estimativa por IP ou da geocodificação, e não do seu endereço. Digitando a cidade no portal, o `ipwho.is` deixa de ser consultado.

O que fica gravado no aparelho: o Wi-Fi e a senha, a cidade digitada, as coordenadas dela e o fuso horário. A memória do ESP32 **não é criptografada** por padrão. **Antes de doar ou vender o aparelho, faça o reset de fábrica** (segurar o botão por 10 s).

Cuidados importantes:

- **O portal de configuração é uma rede aberta, sem senha.** Enquanto ele está aberto, a senha do seu Wi-Fi trafega sem criptografia, e qualquer pessoa ao alcance poderia conectar nele e alterar a configuração. Configure em casa, de preferência rapidamente. Na primeira configuração o portal fica aberto até alguém configurar; depois disso ele só abre por 3 minutos, ao segurar o botão ou após 10 minutos sem Wi-Fi.
- **As consultas HTTPS não validam o certificado** (`setInsecure`). Isso é aceitável em um protótipo porque nenhum dado sensível é enviado, mas alguém com controle da sua rede local poderia exibir clima e localização falsos.
- **O Monitor Serial** mostra apenas o servidor e o código de resposta quando uma consulta falha, sem coordenadas.

---

## ⚠️ Limitações

- **Localização por IP é aproximada** e pode indicar a cidade do seu provedor, não a sua. Se for o caso, digite a cidade no portal.
- **Uso não comercial:** o plano gratuito do Open-Meteo vale apenas para uso não comercial.
- **Serviços gratuitos de terceiros** (ipwho.is e Open-Meteo) podem limitar ou mudar o acesso.
- **Protótipo:** sem proteção do portal, sem validação de certificado e sem criptografia da memória (veja Privacidade e segurança).

---

## 📝 Padrão de commits

Este projeto usa commits com gitmoji. Os detalhes estão em [COMMITS.md](COMMITS.md).

---

## 📄 Licença

Distribuído sob a licença **MIT**. Veja o arquivo [LICENSE](LICENSE).

---

## 👤 Autor

**Maxwell França** · Contador & Administrador | IA, Automação e Sistemas Embarcados  
🇧🇷 São Luís – MA, Brasil

<a href="https://github.com/maxwell-franca">
  <img src="https://img.shields.io/badge/GitHub-maxwell--franca-181717?style=for-the-badge&logo=github" />
</a>

---

⭐ Se gostou do projeto, deixe uma estrela no repositório
