# 📝 Padrão de commits

Este projeto usa commits com **gitmoji**: um emoji no início da mensagem indica o tipo da mudança, seguido de uma frase curta no imperativo.

```
<emoji> <resumo curto da mudança>
```

## Emojis usados

| Emoji | Quando usar | Exemplo |
| --- | --- | --- |
| ✨ | Nova funcionalidade | `✨ Adiciona tela de previsão de 4 dias` |
| 🐛 | Correção de bug | `🐛 Corrige o horário do pôr do sol` |
| ⚡ | Melhoria de desempenho | `⚡ Reduz o uso de memória no JSON do clima` |
| 🔒 | Segurança e privacidade | `🔒 Protege o portal de configuração com senha` |
| 📝 | Documentação | `📝 Atualiza o README com os botões do aparelho` |
| 🎉 | Commit inicial | `🎉 Primeiro commit` |
| ♻️ | Refatoração sem mudar comportamento | `♻️ Separa o desenho dos ícones em funções` |
| 🔧 | Configuração | `🔧 Ajusta .gitignore` |
| 🔥 | Remoção de código ou arquivos | `🔥 Remove tela não utilizada` |

## Regras

- Uma mudança lógica por commit.
- Resumo com até ~70 caracteres, sem ponto final.
- Nunca commitar credenciais: o Wi-Fi é configurado pelo portal do aparelho, não pelo código.
