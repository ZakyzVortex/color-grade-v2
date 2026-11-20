# Color Grade — mod .levipack para LeviLauncher Android

Filtro de cor aplicado na tela inteira do Minecraft Bedrock: brilho, contraste, saturação, gamma,
temperatura, vibrance e vinheta. Feito a partir do `levilauncher-android-mod-template`.

## Como funciona

1. Ao ativar o mod (`enable()`), uma thread espera o `libminecraftpe.so` carregar e troca, na tabela de
   imports dele (GOT/PLT), a função `eglSwapBuffers` pela nossa (`src/render/GotHook.cpp`).
2. A cada quadro, antes de o jogo apresentar a imagem, o mod copia o quadro pronto para uma textura e
   desenha um triângulo de tela cheia com um shader GLSL ES 3.00 (`src/render/ColorGrade.cpp`).
3. O estado do OpenGL é salvo e restaurado, então o jogo não percebe a passada extra.

Como é um filtro sobre a imagem final, não depende de offsets nem assinaturas do jogo e deve continuar
funcionando entre versões (`minecraft_versions` fica `[]`). Se todos os valores estiverem neutros
(brilho 0, contraste 1, saturação 1, gamma 1, resto 0), nenhuma passada extra é feita.

## Menu dentro do jogo (v0.2.0)

O mod registra o módulo **Color Grade** no Mod Menu do LeviLauncher, com sliders para brilho, contraste,
saturação, gamma, temperatura, vibrance e vinheta. Os valores mudam **na hora**, sem reiniciar o jogo, e
são salvos no `config.json` (no máximo uma vez a cada ~1,5 s enquanto você mexe no slider, e ao fechar).
A chave do módulo liga e desliga o filtro. Os mesmos valores continuam editáveis nos detalhes do mod no
launcher, mas esses só valem na próxima vez que o jogo abrir.

## Opções (config.json)

| Campo | Faixa | Neutro | Padrão |
|---|---|---|---|
| `enabled` | true/false | — | true |
| `brightness` | -0.5 a 0.5 | 0 | 0 |
| `contrast` | 0.5 a 1.5 | 1 | 1.05 |
| `saturation` | 0 a 2 | 1 | 1.2 |
| `gamma` | 0.5 a 2 | 1 | 1 |
| `temperature` | -1 a 1 | 0 | 0 |
| `vibrance` | -1 a 1 | 0 | 0 |
| `vignette` | 0 a 1 | 0 | 0 |

Valores fora da faixa são ajustados automaticamente.

## Compilar sem instalar nada (GitHub Actions)

1. Crie um repositório no GitHub e envie estes arquivos (`git push` na branch `main`).
2. Abra a aba **Actions**, entre no workflow **Build** e baixe o artifact `color-grade`.
3. Dentro dele está `color_grade-0.1.0-arm64-v8a.levipack`.
4. No celular: LeviLauncher → Mods → Add Mod → escolha o arquivo.

Também dá para compilar no PC, como no tutorial:

```powershell
$env:ANDROID_HOME = "C:/Users/<voce>/AppData/Local/Android/Sdk"
./scripts/package.ps1 -Abi arm64-v8a
```

## Ver o log

```powershell
adb logcat | Select-String "Color Grade"
```

Linhas esperadas: `Hooked eglSwapBuffers in libminecraftpe.so (N slot(s))` e `Renderer ready (...)`.

## Se algo não funcionar

- **`Could not find an import of eglSwapBuffers`**: o jogo não importa essa função pela tabela normal
  (por exemplo, pega via `eglGetProcAddress`). Seria preciso usar a Hook API do Preloader
  (`pl/memory/Hook.hpp`) no lugar do `GotHook`.
- **`OpenGL ES 3.x is required`**: o jogo está rodando em contexto GLES 2, e o filtro fica inativo.
- **Tela preta ou piscando**: desative `enabled` no config e mande o log.
- **Conflito com outros mods gráficos** (Fluidium, FrameGen etc.): teste Color Grade sozinho primeiro.
- **Erro de compilação**: cole o log do Actions; as chamadas `pl::` do template já foram usadas como estão,
  e o código novo é só C++/EGL/GLES padrão.

## Estrutura

```text
src/main.cpp                  registra o mod
src/mod/MyMod.{h,cpp}         ciclo de vida e registro do módulo no Mod Menu
src/mod/Config.{h,cpp}        opções e esquema do menu de configuração
src/render/GotHook.{h,cpp}    troca imports na GOT/PLT do libminecraftpe.so
src/render/ColorGrade.{h,cpp} shader, cópia do quadro e hook de eglSwapBuffers
```
