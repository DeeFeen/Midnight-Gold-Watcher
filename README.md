# GX - Gold Export

Jednoduchý WoW addon (žádné externí knihovny, vanilla Lua), který sleduje zlato
**všech postav na účtu** a umí ho zobrazit / exportovat pro živý textový overlay
v OBS.

Obsah adresáře:

| Soubor       | Popis                                                            |
| ------------ | ---------------------------------------------------------------- |
| `GX.toc`     | Metadata addonu (Interface: 120100, SavedVariables: `GX_DB`).    |
| `GX_UI.xml`  | Frame šablony zděděné z oficiálních Blizzard šablon.             |
| `GX.lua`            | Veškerá logika addonu.                                           |
| `watcher.py`        | Externí skript pro OBS (spouští se mimo hru).                    |
| `start_watcher.bat` | Dvojklikový spouštěč (malé čisté okno, auto-instalace Pythonu).  |

---

## Instalace

1. Zkopíruj složku `GX` do `World of Warcraft\_retail_\Interface\AddOns\`.
   Výsledná cesta: `...\Interface\AddOns\GX\` (uvnitř musí být `GX.toc`).
2. (Nepovinné) Python skript zkopíruj kamkoli, např. do složky s addonem
   nebo do `C:\GX\` — běží samostatně, nezávisle na hře.
3. Spusť hru, na přihlašovací obrazovce musí být addon **GX** zaškrtnutý
   (po čisté instalaci se automaticky zapíná).

---

## Použití ve hře

Slash příkaz: `/gx`

| Příkaz                 | Význam                                                        |
| ---------------------- | ------------------------------------------------------------- |
| `/gx` nebo `/gx help`  | Vypíše nápovědu.                                              |
| `/gx show`             | Otevře kompaktní okno s celkovým zlatem za celý účet.         |
| `/gx export`           | Otevře okno s kopírovatelným textem aktuálního součtu.        |
| `/gx autosave`         | Zapne/vypne automatický reload (výchozí interval 60 min).     |
| `/gx autosave <min>`   | Nastaví interval v minutách (minimálně 1 minuta).             |
| `/gx autosave off`     | Vypne automatický reload.                                     |
| `/gx settings`         | Otevře nastavení (Esc → Options → AddOns).                    |
| `/gx minimap`          | Zobrazí / skryje kruhovou ikonu u minimapy.                   |
| `/gx reset`            | Smaže všechna uložená data o zlatě.                           |

Okno `/gx show` i `/gx export` se zavírají tlačítkem **X** i klávesou **ESC**.

### Minimap ikona a nastavení

- **Minimap ikona** — kruhová ikona zlata vedle minimapy (pravé tlačítko →
  nastavení, levé → otevře / zavře okno se zlatem, tažením se přesouvá a pozice se ukládá).
  Stejná ikona mince a stejná kruhová maska jako v okně `/gx show`.
- **Nastavení** — `Esc` → `Options` → `AddOns` → **GX - Gold Export** (nebo
  `/gx settings`). Panel umožňuje: zapnout/vypnout minimap ikonu, nastavit
  interval autosave posuvníkem (0 = vypnuto), tlačítko „Save & reload now",
  „Show gold window" a „Reset saved gold". Vše se ukládá okamžitě do `GX_DB`.

### Jak addon sbírá data

Každá postava, se kterou se přihlásíš, si uloží svoje aktuální zlato do tabulky
`GX_DB` (SavedVariables). Údaj se aktualizuje při přihlášení (`PLAYER_LOGIN`)
a při každé změně peněz (`PLAYER_MONEY`).

Zároveň addon automaticky sleduje a započítává **zlato z Warband banky**
(`C_Bank.FetchDepositedMoney`), a to při přihlášení, otevření banky (`BANKFRAME_OPENED`)
nebo při jakékoli změně peněz na účtu (`ACCOUNT_MONEY`).

Protože SavedVariables jsou **per-account** (ne per-character), addon vidí zlato
všech postav i Warband banky, které na účtu máš, a `/gx show` i watcher je sečte.

Soubor na disku vznikne na cestě:

```
WTF\Account\<NázevÚčtu>\SavedVariables\GX.lua
```

> **Upozornění:** Klient zapíše SavedVariables na disk **pouze při `/reload`
> nebo při odhlášení postavy**. Nikdy za běhu živě. To je ovlivněno WoW Lua
> sandboxem (viz níže).

---

## `/gx autosave` — průběžné ukládání

`/gx autosave` zapne opakovaný `ReloadUI()` v nastaveném intervalu. Reload
přiměje klienta zapsat SavedVariables na disk, takže `watcher.py` (a tím i OBS)
dostane čerstvá data i bez ručního reloadu.

**Důležité varování:**

- Reload na ~1 sekundu zmrazí hraní (UI se znovu načte).
- Interval **nastavuj minimálně 60 sekund**; příliš časté reloady ruší hraní.
  Výchozí hodnota je 60 minut.
- Autosave se po každém reloadu sám znovu zapne (nastavení je uložené
  v `GX_DB.autosaveMinutes`). Vypnout: `/gx autosave off`.

---

## Export do OBS (`watcher.py`)

### Proč to nejde „jen" z addonu?

WoW addon běží v **Lua sandboxu bez přístupu k souborům** – neexistuje žádné
`io`/`os` file API. Addon nemůže napsat ani přepisovat `.txt` soubor. Jediné,
co umí „zapsat", je SavedVariables, a to klient fyzicky uloží na disk pouze při
`/reload` nebo odhlášení – **nikdy živě za běhu**. Proto jsou potřeba **dvě
části**:

1. **Addon** ukládá data do `GX.lua` (SavedVariables) a `/gx autosave`
   zajišťuje, že se tyto soubory pravidelně zapisují.
2. **Externí skript** `watcher.py` (mimo hru) hlídá `GX.lua`, sečte zlato
   a přepíše čistý textový soubor `totalgold.txt`, který už OBS umí číst.

### 1) Spuštění skriptu

Skript potřebuje **Python 3.6+**, žádné třetí strany knihovny (čistý standardní
balíček).

**Nejjednodušší spuštění (doporučeno)** – stačí poklepat na soubor **`start_watcher.bat`**.
- Otevře malé, čisté terminálové okno zobrazující pouze celkové zlato a čas aktualizace.
- Pokud na počítači Python chybí, dávkový soubor jej **automaticky nainstaluje z Microsoft Store** (přes winget nebo otevře Store).

**Ruční spuštění přes příkazovou řádku**:
```bat
python watcher.py --compact
```
(případně bez `--compact` pro detailní ladicí výpis).

Pokud máš na instanci víc účtů a chceš sledovat konkrétní (jinak se použije
první nalezený abecedně):

```bat
python watcher.py --account 410566417#1
```

Alternativně lze cestu zadat explicitně:

```bat
python watcher.py --file "C:\World of Warcraft\_retail_\WTF\Account\<NázevÚčtu>\SavedVariables\GX.lua"
```

Nebo nech cestu najít podle instalační složky WoW (složka obsahující `WTF`):

```bat
python watcher.py --wow-root "C:\World of Warcraft\_retail_"
```

Další parametry:

```
--output <cesta>   cílový soubor (default: totalgold.txt ve složce skriptu)
--poll <sekundy>   interval hlídání (default: 2 s)
--raw              do totalgold.txt zapsat jen číslo (raw copper) místo formátu "1,234g 56s 78c"
```

**Pokud skript hlásí „cannot locate GX.lua":** soubor `GX.lua` ve
`WTF\Account\...\SavedVariables\` vytvoří klient až poté, co addon jednou
naběhl a hra zapsala SavedVariables. Přihlas se na postavu, ve hře spusť
`/gx show` a pak `/reload` – soubor se objeví. Skript ho poté už najde a při
každém `/reload` (nebo autosave) přepíše `totalgold.txt`.

**Jak zjistit správnou cestu k `GX.lua` na Windows:** najeď do složky
`WTF\Account\` v instalaci WoW. Podsložky = názvy účtů. Pro tvůj účet pak
`SavedVariables\GX.lua` je přesně ten soubor, který skript sleduje. Pokud
nevíš, kde WoW je: klikni pravým na ikonu hry (Battle.net) → Otevřít v Průzkumníku.

### 2) Napojení v OBS

1. Ve scéně přidej zdroj **Text (GDI+)**.
2. Otevři **Vlastnosti** zdroje.
3. Zaškrtni **„Read from file"** (v češtině „Číst ze souboru").
4. Vyber soubor `totalgold.txt` (cestu vypíše `watcher.py` při startu).
5. Volitelně nastav font, barvu, pozadí.
6. OBS obnoví text při každé změně souboru automaticky. Pokud by se text
   neobnovil, zdroj na chvilku skryj a zase zobraz (toggle visibility).

---

## Edge cases, které addon řeší

- **Nový účet / čistá instalace:** `GX_DB` se inicializuje na prázdnou
  tabulku, `/gx show` zobrazí `0g` bez chyb v `/reload`.
- **Nově přidaná postava:** zlato se uloží při prvním přihlášení.
- **Přejmenování serveru nebo postavy:** starý záznam zůstává pod starým
  klíčem „Jméno-Server"; pokud se součet zdá divný, použij `/gx reset`
  (případně smaž `GX.lua`), přihlas postupně postavy a data se znovu sesbírají.
- **Chybějící soubor SavedVariables:** skript počká, než WoW soubor vytvoří.

---

## Reference na použité Blizzard šablony (wow-ui-source)

Při vývoji byl použit extrahovaný zdroj Blizzard UI (`Gethe/wow-ui-source`,
verze 12.1.0) pouze jako **dokumentace oficiálních šablon a API**. Klíčové
použité položky:

| Šablona / soubor                                        | Použití                                  |
| ------------------------------------------------------- | ---------------------------------------- |
| `Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml` — `PortraitFrameFlatTemplate` | Hlavní okno `/gx show` (portrét, titulek, close button, flat pozadí). |
| `Blizzard_SharedXML/Shared/Dialog/DialogTemplates.xml` — `DialogBorderTemplate` a `DialogHeaderTemplate` | Okno `/gx export` (zaoblený rámeček + DiamondMetal header; border je použitý jako child `BG`, stejně jako u `CreateChannelPopup`). |
| `.../SharedUIPanelTemplates.xml` — `UIPanelCloseButtonDefaultAnchors` | Zavírací tlačítko.                       |
| `Blizzard_SharedXML/Shared/InputBox/InputBoxTemplates.xml` — `InputBoxTemplate` | Kopírovatelný EditBox.                   |
| `Blizzard_SharedXML/FormattingUtil.lua` — `GetMoneyString` | Nativní formátování měny (ikony mincí).  |
| `Blizzard_UIParentPanelManager/Shared/UIParentPanelManager.lua` — `UISpecialFrames` | Zavírání okna klávesou ESC.              |

Díky dědění z oficiálních šablon okno vypadá jako nativní UI aktuálního klienta
a přežije vizuální úpravy Surface – a posteriori. `wow-ui-source` se do addonu
nekopíruje, je to pouze referenční materiál.

---

## Otestované scénáře

- **Nová instalace:** addon se načte, `/gx show` zobrazí `0g` (prázdná DB),
  žádné errory v `/reload`.
- **Více postav:** dvě postavy na účtu, každá uloží vlastní zlato; `/gx show`
  zobrazí součet obou.
- **Reload UI:** `/reload` proběhne čistě, SavedVariables se zapíšou, okna se
  po reloadu dají znovu otevřít.
- **`/gx show`:** okno s titulkem, mincemi zlata, tlačítkem X, ESC i tažením.
- **`/gx export`:** EditBox obsahuje součet (formát + raw copper), text jde
  označit a zkopírovat.
- **`/gx autosave`:** zapnutí/vypnutí, krátký interval (záměrně 1 min)
  → WoW provede reload → `GX.lua` se objeví/zaktualizuje na disku.
- **End-to-end test:** `/gx show` ve hře → `/reload`  → změna `GX.lua`
  zachycena `watcher.py` → `totalgold.txt` se přepíše novým součtem → hodnota
  se zobrazí v testovacím OBS zdroji „Text (GDI+)" s „Read from file".