// Minimal ambient typing for @rebble/clay -- the package ships no types
// of its own, and its actual runtime surface (from this file's own use of
// it) is just "construct it with a config array"; new(config: object[])
// is that whole contract typed, not a partial stub of a larger API.
interface ClayInstance {}
interface ClayFactory {
  new (schema: object[]): ClayInstance;
}

/**
 * Localization (direct ask: "add localization where it is easy"). Config-
 * page text only -- this file runs as real browser JS in the Clay page's
 * own webview (confirmed: readClaySettings() in index.ts already reads
 * `localStorage` from this same environment), full Unicode, no font
 * constraints, unlike the watch's own bitmap glyph-atlas text (see
 * Development_Guidance.org's Glyph-atlas section for why *that* half is a
 * much bigger job, not attempted here). English is the default -- USA is
 * this project's own default throughout (temp/wind/precip units, etc.) --
 * with Spanish/French/German/Italian/Portuguese/Dutch as the "easy"
 * (Latin-alphabet, no watch-side font work) set.
 *
 * Static, not dynamically retranslated: this whole `config` array is built
 * once, before Clay ever renders the page, so every label already needs
 * its final text at that point. Changing the Language select and hitting
 * Save takes effect the *next* time this config page is opened, not live
 * -- CONFIG_LANGUAGE is read back from the same `clay-settings` localStorage
 * cache readClaySettings() (index.ts) already relies on, before Clay's own
 * constructor runs.
 */
const SUPPORTED_LANGS = ['EN', 'ES', 'FR', 'DE', 'IT', 'PT', 'NL'] as const;
type LangCode = typeof SUPPORTED_LANGS[number];

interface Strings {
  appTitle: string;
  langLabel: string;
  headingFeatures: string;
  labelTapTimeout: string;
  opt3s: string;
  opt5s: string;
  opt10s: string;
  labelAaTime: string;
  labelAaDate: string;
  labelAaWind: string;
  labelAaTemp: string;
  labelRingLayout: string;
  optRingIconsOut: string;
  optRingTextOut: string;
  optRingIconsOnly: string;
  optRingTextOnly: string;
  labelRingTextTint: string;
  headingWeatherInfo: string;
  textWeatherInfo: string;
  labelInfoTemp: string;
  labelInfoFeelsLike: string;
  labelInfoPrecip: string;
  labelInfoWind: string;
  labelInfoHumidity: string;
  labelInfoGust: string;
  labelInfoUv: string;
  labelInfoAqi: string;
  labelInfoSteps: string;
  labelInfoSleep: string;
  headingDateTime: string;
  labelDateFormat: string;
  optDateDdMonYyyy: string;
  optDateDowDdMon: string;
  labelPadDay: string;
  labelPadHour: string;
  textAmPm: string;
  labelShowAm: string;
  labelShowPm: string;
  headingFitness: string;
  textFitness: string;
  labelShowSteps: string;
  labelShowSleep: string;
  labelHealthIntensity: string;
  optHealthIntensity100: string;
  labelBatteryScale: string;
  textBatteryScale: string;
  optBatteryScale100: string;
  headingCalendar: string;
  labelShowNextAppt: string;
  textCalendarUrl: string;
  labelCalendarUrl: string;
  labelReminderIntensity: string;
  optIntensity100: string;
  optIntensity75: string;
  optIntensity50: string;
  optIntensity25: string;
  labelHideApptTitle: string;
  headingMoon: string;
  labelMoonDisplay: string;
  optMoonAlways: string;
  optMoonNight: string;
  optMoonVisible: string;
  optMoonNever: string;
  headingColors: string;
  textColorsOnly: string;
  labelColorBgDay: string;
  labelColorBgNight: string;
  optColorBlack: string;
  optColorWhite: string;
  optColorOxfordBlue: string;
  optColorBulgarianRose: string;
  optColorDarkGreen: string;
  optColorChromeYellow: string;
  labelTempColorSource: string;
  optRealfeel: string;
  optActual: string;
  labelTempColorScale: string;
  optTempColorScaleF: string;
  optTempColorScaleC: string;
  headingUnits: string;
  labelTempUnit: string;
  optCelsius: string;
  optFahrenheit: string;
  labelWindUnit: string;
  optMph: string;
  optKph: string;
  labelPrecipUnit: string;
  optInches: string;
  optMillimeters: string;
  headingAdvanced: string;
  labelAllowSweep: string;
  headingCredits: string;
  textCredits: string;
  submitSave: string;
}

/**
 * Third-party credits (see moderne/THIRD_PARTY_NOTICES.md for the full
 * writeup) -- deliberately NOT translated per-language like the rest of
 * this file: it's proper nouns and license names (Open-Meteo, SIL OFL,
 * MPL-2.0, etc.), which stay in their own original form regardless of
 * locale in virtually every other app's own credits/about screen too.
 * Shared as one constant, not repeated 7 times per language block below,
 * so there's a single place to update if a dependency/license ever
 * changes -- unlike every other string here, this one is never meant to
 * actually differ between languages.
 */
const CREDITS_TEXT =
  'Weather data: Open-Meteo.com (CC BY 4.0). Icons: Weather Icons by Erik Flowers (SIL OFL 1.1), '
  + 'Material Design Icons by Pictogrammers (Apache 2.0). Font: Poiret One (SIL OFL 1.1). '
  + 'Moon position/phase math ported from SunCalc by Vladimir Agafonkin (BSD-2-Clause). '
  + 'Calendar parsing: ical.js (MPL-2.0). Config page: Clay (MIT).';

const EN: Strings = {
  appTitle: 'Moderne Configuration',
  langLabel: 'Language',
  headingFeatures: 'Features',
  labelTapTimeout: 'Tap/shake Timeout',
  opt3s: '3 seconds',
  opt5s: '5 seconds',
  opt10s: '10 seconds',
  labelAaTime: 'Anti-aliased Time Font',
  labelAaDate: 'Anti-aliased Date Font',
  labelAaWind: 'Anti-aliased Weather Big Font',
  labelAaTemp: 'Anti-aliased Weather Small Font',
  labelRingLayout: 'Weather Ring Layout',
  optRingIconsOut: 'Icons outer, temp/rain% inner',
  optRingTextOut: 'Temp/rain% outer, icons inner',
  optRingIconsOnly: 'Icons only',
  optRingTextOnly: 'Temp/rain% only',
  labelRingTextTint: 'Ring Text Matches Icon Color',
  headingWeatherInfo: 'Weather Detail Info',
  textWeatherInfo:
    'Up to 4 of these show at once (6 if Weather Ring Layout above is set to an '
    + '“only” option), most important first: Temperature, Feels Like, '
    + 'Precipitation, Wind, Humidity, Wind Gusts, UV Index, Air Quality, Steps, '
    + 'Sleep. Checking '
    + 'more than the limit just adds lower-priority options that won’t show '
    + 'unless a higher one is unchecked. Defaults to just Wind and Humidity, this '
    + 'block’s original content.',
  labelInfoTemp: 'Temperature',
  labelInfoFeelsLike: 'Feels Like',
  labelInfoPrecip: 'Precipitation (current)',
  labelInfoWind: 'Wind',
  labelInfoHumidity: 'Humidity',
  labelInfoGust: 'Wind Gusts',
  labelInfoUv: 'UV Index',
  labelInfoAqi: 'Air Quality (AQI)',
  labelInfoSteps: 'Steps',
  labelInfoSleep: 'Sleep',
  headingDateTime: 'Date & Time',
  labelDateFormat: 'Date Format',
  optDateDdMonYyyy: 'Day Month Year (08 Aug 2026)',
  optDateDowDdMon: 'Weekday Day Month (Wed 08 Aug)',
  labelPadDay: 'Zero-pad Day (08 vs 8)',
  labelPadHour: 'Zero-pad Hour (08:30 vs 8:30)',
  textAmPm:
    'No effect in 24-hour mode. Independent toggles, not one option: turning on '
    + 'just one (e.g. Show AM) shows that marker only on its half of the day, so '
    + 'its absence tells you it’s the other half.',
  labelShowAm: 'Show AM',
  labelShowPm: 'Show PM',
  headingFitness: 'Fitness',
  textFitness:
    'Steps and sleep, from HealthService, as a compact row under the date: '
    + 'a footprint icon for steps, a crescent moon for sleep. Independent '
    + 'toggles -- either, both, or neither.',
  labelShowSteps: 'Show Steps',
  labelShowSleep: 'Show Sleep',
  labelHealthIntensity: 'Health Line Intensity',
  optHealthIntensity100: '100% (full strength)',
  labelBatteryScale: 'Battery Bar Scale',
  textBatteryScale:
    'The bar under the time shows a fraction of this threshold, not raw '
    + 'battery percent -- it stays full while charge is above the selected '
    + 'value, then drains proportionally below it. E.g. at 50%, an actual '
    + 'charge of 25% shows a half-full bar. 100% shows actual charge '
    + 'directly, unscaled.',
  optBatteryScale100: '100% (matches actual charge)',
  headingCalendar: 'Calendar',
  labelShowNextAppt: 'Show Next Appointment',
  textCalendarUrl:
    'Paste your calendar’s private iCal/ICS subscription URL below (Google '
    + 'Calendar: Settings > your calendar > Integrate calendar > Secret address '
    + 'in iCal format). This URL grants read access to your whole calendar to '
    + 'anyone who has it -- treat it like a password. Leave blank to see a '
    + 'reminder to set this up instead.',
  labelCalendarUrl: 'Calendar ICS URL',
  labelReminderIntensity: 'Reminder Text Intensity',
  optIntensity100: '100% (matches date text)',
  optIntensity75: '75%',
  optIntensity50: '50%',
  optIntensity25: '25%',
  labelHideApptTitle: 'Hide Appointment Title (show time only)',
  headingMoon: 'Moon',
  labelMoonDisplay: 'Moon Icon',
  optMoonAlways: 'Always (dimmed when not visible)',
  optMoonNight: 'Night only (hidden in daytime)',
  optMoonVisible: 'Visible only (no dimmed preview)',
  optMoonNever: 'Never',
  headingColors: 'Colors',
  textColorsOnly: 'Only available on color watches.',
  labelColorBgDay: 'Background Color (Day)',
  labelColorBgNight: 'Background Color (Night)',
  optColorBlack: 'Black',
  optColorWhite: 'White',
  optColorOxfordBlue: 'Oxford Blue',
  optColorBulgarianRose: 'Bulgarian Rose',
  optColorDarkGreen: 'Dark Green',
  optColorChromeYellow: 'Chrome Yellow',
  labelTempColorSource: 'Weather Icon Color Based On',
  optRealfeel: 'RealFeel (wind chill/heat index)',
  optActual: 'Actual Temperature',
  labelTempColorScale: 'Temp Color Bin Scale',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Units',
  labelTempUnit: 'Temperature Unit',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Wind Speed Unit',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Precipitation Unit',
  optInches: 'Inches',
  optMillimeters: 'Millimeters',
  headingAdvanced: 'Advanced',
  labelAllowSweep: 'Allow sweep tests',
  headingCredits: 'Credits',
  textCredits: CREDITS_TEXT,
  submitSave: 'Save',
};

const ES: Strings = {
  appTitle: 'Configuración de Moderne',
  langLabel: 'Idioma',
  headingFeatures: 'Funciones',
  labelTapTimeout: 'Tiempo de espera al tocar/agitar',
  opt3s: '3 segundos',
  opt5s: '5 segundos',
  opt10s: '10 segundos',
  labelAaTime: 'Fuente de la hora suavizada',
  labelAaDate: 'Fuente de la fecha suavizada',
  labelAaWind: 'Fuente grande del clima suavizada',
  labelAaTemp: 'Fuente pequeña del clima suavizada',
  labelRingLayout: 'Diseño del anillo del clima',
  optRingIconsOut: 'Iconos fuera, temp./lluvia % dentro',
  optRingTextOut: 'Temp./lluvia % fuera, iconos dentro',
  optRingIconsOnly: 'Solo iconos',
  optRingTextOnly: 'Solo temp./lluvia %',
  labelRingTextTint: 'El texto del anillo coincide con el color del icono',
  headingWeatherInfo: 'Información detallada del clima',
  textWeatherInfo:
    'Se muestran hasta 4 a la vez (6 si el diseño del anillo del clima está '
    + 'en una opción de "solo"), por orden de importancia: temperatura, '
    + 'sensación térmica, precipitación, viento, humedad, ráfagas de viento, '
    + 'índice UV, calidad del aire, pasos, sueño. Marcar más del límite solo añade opciones de '
    + 'menor prioridad que no se mostrarán a menos que se desmarque una de mayor '
    + 'prioridad. Por defecto solo viento y humedad, el contenido original de '
    + 'este bloque.',
  labelInfoTemp: 'Temperatura',
  labelInfoFeelsLike: 'Sensación térmica',
  labelInfoPrecip: 'Precipitación (actual)',
  labelInfoWind: 'Viento',
  labelInfoHumidity: 'Humedad',
  labelInfoGust: 'Ráfagas de viento',
  labelInfoUv: 'Índice UV',
  labelInfoAqi: 'Calidad del aire (AQI)',
  labelInfoSteps: 'Pasos',
  labelInfoSleep: 'Sueño',
  headingDateTime: 'Fecha y hora',
  labelDateFormat: 'Formato de fecha',
  optDateDdMonYyyy: 'Día mes año (08 ago 2026)',
  optDateDowDdMon: 'Día semana, día mes (mié 08 ago)',
  labelPadDay: 'Rellenar día con cero (08 frente a 8)',
  labelPadHour: 'Rellenar hora con cero (08:30 frente a 8:30)',
  textAmPm:
    'No tiene efecto en el modo de 24 horas. Interruptores independientes, no '
    + 'una sola opción: activar solo uno (p. ej. Mostrar AM) muestra esa marca '
    + 'solo en su mitad del día, así que su ausencia indica la otra mitad.',
  labelShowAm: 'Mostrar AM',
  labelShowPm: 'Mostrar PM',
  headingFitness: 'Actividad física',
  textFitness:
    'Pasos y sueño, desde HealthService, como una fila compacta bajo la '
    + 'fecha: un icono de huella para los pasos, una luna creciente para el '
    + 'sueño. Interruptores independientes: uno, ambos o ninguno.',
  labelShowSteps: 'Mostrar pasos',
  labelShowSleep: 'Mostrar sueño',
  labelHealthIntensity: 'Intensidad de la línea de salud',
  optHealthIntensity100: '100% (intensidad total)',
  labelBatteryScale: 'Escala de la barra de batería',
  textBatteryScale:
    'La barra bajo la hora muestra una fracción de este umbral, no el '
    + 'porcentaje real de batería: permanece llena mientras la carga esté '
    + 'por encima del valor elegido, y luego se vacía proporcionalmente por '
    + 'debajo de él. Por ejemplo, con 50%, una carga real del 25% muestra '
    + 'una barra medio llena. 100% muestra la carga real directamente, sin '
    + 'escalar.',
  optBatteryScale100: '100% (coincide con la carga real)',
  headingCalendar: 'Calendario',
  labelShowNextAppt: 'Mostrar próxima cita',
  textCalendarUrl:
    'Pega abajo la URL privada de suscripción iCal/ICS de tu calendario '
    + '(Google Calendar: Configuración > tu calendario > Integrar calendario > '
    + 'Dirección secreta en formato iCal). Esta URL da acceso de lectura a todo '
    + 'tu calendario a quien la tenga; trátala como una contraseña. Déjala en '
    + 'blanco para ver un recordatorio de configurarlo.',
  labelCalendarUrl: 'URL ICS del calendario',
  labelReminderIntensity: 'Intensidad del texto del recordatorio',
  optIntensity100: '100% (igual que el texto de la fecha)',
  optIntensity75: '75%',
  optIntensity50: '50%',
  optIntensity25: '25%',
  labelHideApptTitle: 'Ocultar título de la cita (mostrar solo la hora)',
  headingMoon: 'Luna',
  labelMoonDisplay: 'Icono de la luna',
  optMoonAlways: 'Siempre (atenuado cuando no es visible)',
  optMoonNight: 'Solo de noche (oculto de día)',
  optMoonVisible: 'Solo visible (sin vista previa atenuada)',
  optMoonNever: 'Nunca',
  headingColors: 'Colores',
  textColorsOnly: 'Solo disponible en relojes en color.',
  labelColorBgDay: 'Color de fondo (día)',
  labelColorBgNight: 'Color de fondo (noche)',
  optColorBlack: 'Negro',
  optColorWhite: 'Blanco',
  optColorOxfordBlue: 'Azul Oxford',
  optColorBulgarianRose: 'Rosa búlgaro',
  optColorDarkGreen: 'Verde oscuro',
  optColorChromeYellow: 'Amarillo cromo',
  labelTempColorSource: 'Color del icono del clima basado en',
  optRealfeel: 'RealFeel (sensación de viento/calor)',
  optActual: 'Temperatura real',
  labelTempColorScale: 'Escala de las franjas de color por temperatura',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Unidades',
  labelTempUnit: 'Unidad de temperatura',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Unidad de velocidad del viento',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Unidad de precipitación',
  optInches: 'Pulgadas',
  optMillimeters: 'Milímetros',
  headingAdvanced: 'Avanzado',
  labelAllowSweep: 'Permitir pruebas de barrido',
  headingCredits: 'Créditos',
  textCredits: CREDITS_TEXT,
  submitSave: 'Guardar',
};

const FR: Strings = {
  appTitle: 'Configuration Moderne',
  langLabel: 'Langue',
  headingFeatures: 'Fonctionnalités',
  labelTapTimeout: 'Délai de tapotement/secousse',
  opt3s: '3 secondes',
  opt5s: '5 secondes',
  opt10s: '10 secondes',
  labelAaTime: 'Police de l’heure lissée',
  labelAaDate: 'Police de la date lissée',
  labelAaWind: 'Grande police météo lissée',
  labelAaTemp: 'Petite police météo lissée',
  labelRingLayout: 'Disposition de l’anneau météo',
  optRingIconsOut: 'Icônes à l’extérieur, temp./pluie % à l’intérieur',
  optRingTextOut: 'Temp./pluie % à l’extérieur, icônes à l’intérieur',
  optRingIconsOnly: 'Icônes seulement',
  optRingTextOnly: 'Temp./pluie % seulement',
  labelRingTextTint: 'Le texte de l’anneau reprend la couleur de l’icône',
  headingWeatherInfo: 'Détails météo',
  textWeatherInfo:
    'Jusqu’à 4 s’affichent à la fois (6 si la disposition de l’anneau météo '
    + 'ci-dessus est réglée sur une option "seulement"), par ordre '
    + 'd’importance : température, ressenti, précipitations, vent, humidité, '
    + 'rafales de vent, indice UV, qualité de l’air, pas, sommeil. Cocher plus que la limite '
    + 'ajoute seulement des options de priorité inférieure qui ne s’afficheront '
    + 'pas tant qu’une option prioritaire n’est pas décochée. Par défaut, seuls '
    + 'vent et humidité, le contenu d’origine de ce bloc.',
  labelInfoTemp: 'Température',
  labelInfoFeelsLike: 'Ressenti',
  labelInfoPrecip: 'Précipitations (actuelles)',
  labelInfoWind: 'Vent',
  labelInfoHumidity: 'Humidité',
  labelInfoGust: 'Rafales de vent',
  labelInfoUv: 'Indice UV',
  labelInfoAqi: 'Qualité de l’air (AQI)',
  labelInfoSteps: 'Pas',
  labelInfoSleep: 'Sommeil',
  headingDateTime: 'Date et heure',
  labelDateFormat: 'Format de date',
  optDateDdMonYyyy: 'Jour mois année (08 août 2026)',
  optDateDowDdMon: 'Jour semaine, jour mois (mer. 08 août)',
  labelPadDay: 'Zéro devant le jour (08 vs 8)',
  labelPadHour: 'Zéro devant l’heure (08:30 vs 8:30)',
  textAmPm:
    'Aucun effet en mode 24 heures. Deux interrupteurs indépendants, pas une '
    + 'seule option : n’activer que l’un (par ex. Afficher AM) montre ce repère '
    + 'uniquement sur sa moitié de journée, donc son absence indique l’autre '
    + 'moitié.',
  labelShowAm: 'Afficher AM',
  labelShowPm: 'Afficher PM',
  headingFitness: 'Activité physique',
  textFitness:
    'Pas et sommeil, via HealthService, sous forme de ligne compacte sous '
    + 'la date : une empreinte de pas pour les pas, un croissant de lune '
    + 'pour le sommeil. Interrupteurs indépendants -- l’un, les deux, ou '
    + 'aucun.',
  labelShowSteps: 'Afficher les pas',
  labelShowSleep: 'Afficher le sommeil',
  labelHealthIntensity: 'Intensité de la ligne santé',
  optHealthIntensity100: '100 % (pleine intensité)',
  labelBatteryScale: 'Échelle de la barre de batterie',
  textBatteryScale:
    'La barre sous l’heure affiche une fraction de ce seuil, pas le '
    + 'pourcentage réel de batterie : elle reste pleine tant que la charge '
    + 'est supérieure à la valeur choisie, puis se vide proportionnellement '
    + 'en dessous. Par exemple, à 50 %, une charge réelle de 25 % affiche '
    + 'une barre à moitié pleine. 100 % affiche la charge réelle '
    + 'directement, sans mise à l’échelle.',
  optBatteryScale100: '100 % (correspond à la charge réelle)',
  headingCalendar: 'Calendrier',
  labelShowNextAppt: 'Afficher le prochain rendez-vous',
  textCalendarUrl:
    'Collez ci-dessous l’URL d’abonnement iCal/ICS privée de votre calendrier '
    + '(Google Agenda : Paramètres > votre agenda > Intégrer l’agenda > Adresse '
    + 'secrète au format iCal). Cette URL donne un accès en lecture à tout votre '
    + 'calendrier à quiconque la possède -- traitez-la comme un mot de passe. '
    + 'Laissez vide pour voir un rappel de configuration à la place.',
  labelCalendarUrl: 'URL ICS du calendrier',
  labelReminderIntensity: 'Intensité du texte du rappel',
  optIntensity100: '100 % (comme le texte de la date)',
  optIntensity75: '75 %',
  optIntensity50: '50 %',
  optIntensity25: '25 %',
  labelHideApptTitle: 'Masquer le titre du rendez-vous (heure seule)',
  headingMoon: 'Lune',
  labelMoonDisplay: 'Icône de la lune',
  optMoonAlways: 'Toujours (atténuée si non visible)',
  optMoonNight: 'Nuit seulement (masquée le jour)',
  optMoonVisible: 'Visible seulement (pas d’aperçu atténué)',
  optMoonNever: 'Jamais',
  headingColors: 'Couleurs',
  textColorsOnly: 'Disponible uniquement sur les montres couleur.',
  labelColorBgDay: 'Couleur de fond (jour)',
  labelColorBgNight: 'Couleur de fond (nuit)',
  optColorBlack: 'Noir',
  optColorWhite: 'Blanc',
  optColorOxfordBlue: 'Bleu Oxford',
  optColorBulgarianRose: 'Rose bulgare',
  optColorDarkGreen: 'Vert foncé',
  optColorChromeYellow: 'Jaune chrome',
  labelTempColorSource: 'Couleur de l’icône météo basée sur',
  optRealfeel: 'RealFeel (refroidissement/indice de chaleur)',
  optActual: 'Température réelle',
  labelTempColorScale: 'Échelle des paliers de couleur (température)',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Unités',
  labelTempUnit: 'Unité de température',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Unité de vitesse du vent',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Unité de précipitations',
  optInches: 'Pouces',
  optMillimeters: 'Millimètres',
  headingAdvanced: 'Avancé',
  labelAllowSweep: 'Autoriser les tests de balayage',
  headingCredits: 'Crédits',
  textCredits: CREDITS_TEXT,
  submitSave: 'Enregistrer',
};

const DE: Strings = {
  appTitle: 'Moderne-Konfiguration',
  langLabel: 'Sprache',
  headingFeatures: 'Funktionen',
  labelTapTimeout: 'Tipp-/Schüttel-Zeitlimit',
  opt3s: '3 Sekunden',
  opt5s: '5 Sekunden',
  opt10s: '10 Sekunden',
  labelAaTime: 'Geglättete Uhrzeit-Schrift',
  labelAaDate: 'Geglättete Datums-Schrift',
  labelAaWind: 'Geglättete große Wetter-Schrift',
  labelAaTemp: 'Geglättete kleine Wetter-Schrift',
  labelRingLayout: 'Wetter-Ring-Layout',
  optRingIconsOut: 'Symbole außen, Temp./Regen % innen',
  optRingTextOut: 'Temp./Regen % außen, Symbole innen',
  optRingIconsOnly: 'Nur Symbole',
  optRingTextOnly: 'Nur Temp./Regen %',
  labelRingTextTint: 'Ringtext passt Symbolfarbe an',
  headingWeatherInfo: 'Wetterdetails',
  textWeatherInfo:
    'Bis zu 4 werden gleichzeitig angezeigt (6, wenn das Wetter-Ring-Layout '
    + 'oben auf eine "Nur"-Option eingestellt ist), nach Wichtigkeit sortiert: '
    + 'Temperatur, Gefühlt, Niederschlag, Wind, Luftfeuchtigkeit, Windböen, '
    + 'UV-Index, Luftqualität, Schritte, Schlaf. Werden mehr als das Limit ausgewählt, werden nur '
    + 'weitere Optionen niedrigerer Priorität hinzugefügt, die erst angezeigt '
    + 'werden, wenn eine höhere abgewählt wird. Standardmäßig nur Wind und '
    + 'Luftfeuchtigkeit, der ursprüngliche Inhalt dieses Blocks.',
  labelInfoTemp: 'Temperatur',
  labelInfoFeelsLike: 'Gefühlt',
  labelInfoPrecip: 'Niederschlag (aktuell)',
  labelInfoWind: 'Wind',
  labelInfoHumidity: 'Luftfeuchtigkeit',
  labelInfoGust: 'Windböen',
  labelInfoUv: 'UV-Index',
  labelInfoAqi: 'Luftqualität (AQI)',
  labelInfoSteps: 'Schritte',
  labelInfoSleep: 'Schlaf',
  headingDateTime: 'Datum & Uhrzeit',
  labelDateFormat: 'Datumsformat',
  optDateDdMonYyyy: 'Tag Monat Jahr (08 Aug 2026)',
  optDateDowDdMon: 'Wochentag Tag Monat (Mi 08 Aug)',
  labelPadDay: 'Tag mit Null auffüllen (08 statt 8)',
  labelPadHour: 'Stunde mit Null auffüllen (08:30 statt 8:30)',
  textAmPm:
    'Im 24-Stunden-Modus ohne Wirkung. Zwei unabhängige Schalter, keine '
    + 'einzelne Option: Wird nur einer aktiviert (z. B. AM anzeigen), erscheint '
    + 'diese Markierung nur in dieser Tageshälfte -- ihr Fehlen zeigt dann die '
    + 'andere Hälfte an.',
  labelShowAm: 'AM anzeigen',
  labelShowPm: 'PM anzeigen',
  headingFitness: 'Fitness',
  textFitness:
    'Schritte und Schlaf, über HealthService, als kompakte Zeile unter dem '
    + 'Datum: ein Fußabdruck-Symbol für Schritte, eine Mondsichel für den '
    + 'Schlaf. Unabhängige Schalter -- eines, beide oder keines.',
  labelShowSteps: 'Schritte anzeigen',
  labelShowSleep: 'Schlaf anzeigen',
  labelHealthIntensity: 'Intensität der Gesundheitszeile',
  optHealthIntensity100: '100 % (volle Intensität)',
  labelBatteryScale: 'Skala der Akkuanzeige',
  textBatteryScale:
    'Der Balken unter der Uhrzeit zeigt einen Anteil dieses Schwellenwerts, '
    + 'nicht den tatsächlichen Akkustand -- er bleibt voll, solange der '
    + 'Ladestand über dem gewählten Wert liegt, und leert sich darunter '
    + 'proportional. Beispiel: bei 50 % zeigt ein tatsächlicher Ladestand '
    + 'von 25 % einen halb vollen Balken. 100 % zeigt den tatsächlichen '
    + 'Ladestand direkt, ohne Skalierung.',
  optBatteryScale100: '100 % (entspricht dem tatsächlichen Ladestand)',
  headingCalendar: 'Kalender',
  labelShowNextAppt: 'Nächsten Termin anzeigen',
  textCalendarUrl:
    'Fügen Sie unten die private iCal/ICS-Abo-URL Ihres Kalenders ein (Google '
    + 'Kalender: Einstellungen > Ihr Kalender > Kalender einbinden > Geheime '
    + 'Adresse im iCal-Format). Diese URL gewährt jedem, der sie besitzt, '
    + 'Lesezugriff auf Ihren gesamten Kalender -- behandeln Sie sie wie ein '
    + 'Passwort. Leer lassen, um stattdessen eine Erinnerung zur Einrichtung '
    + 'zu sehen.',
  labelCalendarUrl: 'Kalender-ICS-URL',
  labelReminderIntensity: 'Textintensität der Erinnerung',
  optIntensity100: '100 % (wie der Datumstext)',
  optIntensity75: '75 %',
  optIntensity50: '50 %',
  optIntensity25: '25 %',
  labelHideApptTitle: 'Termintitel ausblenden (nur Uhrzeit anzeigen)',
  headingMoon: 'Mond',
  labelMoonDisplay: 'Mondsymbol',
  optMoonAlways: 'Immer (abgeblendet, wenn nicht sichtbar)',
  optMoonNight: 'Nur nachts (tagsüber ausgeblendet)',
  optMoonVisible: 'Nur wenn sichtbar (keine abgeblendete Vorschau)',
  optMoonNever: 'Nie',
  headingColors: 'Farben',
  textColorsOnly: 'Nur bei Farbdisplays verfügbar.',
  labelColorBgDay: 'Hintergrundfarbe (Tag)',
  labelColorBgNight: 'Hintergrundfarbe (Nacht)',
  optColorBlack: 'Schwarz',
  optColorWhite: 'Weiß',
  optColorOxfordBlue: 'Oxford-Blau',
  optColorBulgarianRose: 'Bulgarisches Rosa',
  optColorDarkGreen: 'Dunkelgrün',
  optColorChromeYellow: 'Chromgelb',
  labelTempColorSource: 'Wettersymbolfarbe basiert auf',
  optRealfeel: 'RealFeel (gefühlte Temperatur)',
  optActual: 'Tatsächliche Temperatur',
  labelTempColorScale: 'Skala der Temperatur-Farbstufen',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Einheiten',
  labelTempUnit: 'Temperatureinheit',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Windgeschwindigkeitseinheit',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Niederschlagseinheit',
  optInches: 'Zoll',
  optMillimeters: 'Millimeter',
  headingAdvanced: 'Erweitert',
  labelAllowSweep: 'Sweep-Tests zulassen',
  headingCredits: 'Danksagungen',
  textCredits: CREDITS_TEXT,
  submitSave: 'Speichern',
};

const IT: Strings = {
  appTitle: 'Configurazione Moderne',
  langLabel: 'Lingua',
  headingFeatures: 'Funzionalità',
  labelTapTimeout: 'Timeout tocco/scuotimento',
  opt3s: '3 secondi',
  opt5s: '5 secondi',
  opt10s: '10 secondi',
  labelAaTime: 'Font ora con antialiasing',
  labelAaDate: 'Font data con antialiasing',
  labelAaWind: 'Font meteo grande con antialiasing',
  labelAaTemp: 'Font meteo piccolo con antialiasing',
  labelRingLayout: 'Layout dell’anello meteo',
  optRingIconsOut: 'Icone esterne, temp./pioggia % interne',
  optRingTextOut: 'Temp./pioggia % esterne, icone interne',
  optRingIconsOnly: 'Solo icone',
  optRingTextOnly: 'Solo temp./pioggia %',
  labelRingTextTint: 'Il testo dell’anello riprende il colore dell’icona',
  headingWeatherInfo: 'Dettagli meteo',
  textWeatherInfo:
    'Ne vengono mostrati fino a 4 alla volta (6 se il layout dell’anello meteo '
    + 'sopra è impostato su un’opzione "solo"), in ordine di importanza: '
    + 'temperatura, percepita, precipitazioni, vento, umidità, raffiche di '
    + 'vento, indice UV, qualità dell’aria, passi, sonno. Selezionarne più del limite aggiunge '
    + 'solo opzioni a priorità inferiore, che non verranno mostrate finché non '
    + 'se ne deseleziona una più prioritaria. Di default solo vento e umidità, '
    + 'il contenuto originale di questo blocco.',
  labelInfoTemp: 'Temperatura',
  labelInfoFeelsLike: 'Percepita',
  labelInfoPrecip: 'Precipitazioni (attuali)',
  labelInfoWind: 'Vento',
  labelInfoHumidity: 'Umidità',
  labelInfoGust: 'Raffiche di vento',
  labelInfoUv: 'Indice UV',
  labelInfoAqi: 'Qualità dell’aria (AQI)',
  labelInfoSteps: 'Passi',
  labelInfoSleep: 'Sonno',
  headingDateTime: 'Data e ora',
  labelDateFormat: 'Formato data',
  optDateDdMonYyyy: 'Giorno mese anno (08 ago 2026)',
  optDateDowDdMon: 'Giorno settimana, giorno mese (mer 08 ago)',
  labelPadDay: 'Giorno con zero iniziale (08 anziché 8)',
  labelPadHour: 'Ora con zero iniziale (08:30 anziché 8:30)',
  textAmPm:
    'Nessun effetto in modalità 24 ore. Due interruttori indipendenti, non '
    + 'un’unica opzione: attivandone solo uno (es. Mostra AM) quel marcatore '
    + 'appare solo nella sua metà giornata, quindi la sua assenza indica '
    + 'l’altra metà.',
  labelShowAm: 'Mostra AM',
  labelShowPm: 'Mostra PM',
  headingFitness: 'Attività fisica',
  textFitness:
    'Passi e sonno, tramite HealthService, come riga compatta sotto la '
    + 'data: un’icona di impronta per i passi, una luna crescente per il '
    + 'sonno. Interruttori indipendenti -- uno, entrambi o nessuno.',
  labelShowSteps: 'Mostra passi',
  labelShowSleep: 'Mostra sonno',
  labelHealthIntensity: 'Intensità della riga salute',
  optHealthIntensity100: '100% (intensità piena)',
  labelBatteryScale: 'Scala della barra della batteria',
  textBatteryScale:
    'La barra sotto l’ora mostra una frazione di questa soglia, non la '
    + 'percentuale reale della batteria: rimane piena finché la carica è '
    + 'superiore al valore scelto, poi si svuota proporzionalmente al di '
    + 'sotto. Ad esempio, al 50%, una carica reale del 25% mostra una '
    + 'barra mezza piena. 100% mostra la carica reale direttamente, senza '
    + 'scalatura.',
  optBatteryScale100: '100% (corrisponde alla carica reale)',
  headingCalendar: 'Calendario',
  labelShowNextAppt: 'Mostra il prossimo appuntamento',
  textCalendarUrl:
    'Incolla qui sotto l’URL privato di abbonamento iCal/ICS del tuo '
    + 'calendario (Google Calendar: Impostazioni > il tuo calendario > Integra '
    + 'calendario > Indirizzo segreto in formato iCal). Questo URL garantisce a '
    + 'chiunque lo possieda l’accesso in lettura all’intero calendario -- '
    + 'trattalo come una password. Lascia vuoto per vedere invece un '
    + 'promemoria per configurarlo.',
  labelCalendarUrl: 'URL ICS del calendario',
  labelReminderIntensity: 'Intensità testo promemoria',
  optIntensity100: '100% (come il testo della data)',
  optIntensity75: '75%',
  optIntensity50: '50%',
  optIntensity25: '25%',
  labelHideApptTitle: 'Nascondi titolo appuntamento (solo orario)',
  headingMoon: 'Luna',
  labelMoonDisplay: 'Icona della luna',
  optMoonAlways: 'Sempre (attenuata quando non visibile)',
  optMoonNight: 'Solo di notte (nascosta di giorno)',
  optMoonVisible: 'Solo se visibile (nessuna anteprima attenuata)',
  optMoonNever: 'Mai',
  headingColors: 'Colori',
  textColorsOnly: 'Disponibile solo su orologi a colori.',
  labelColorBgDay: 'Colore sfondo (giorno)',
  labelColorBgNight: 'Colore sfondo (notte)',
  optColorBlack: 'Nero',
  optColorWhite: 'Bianco',
  optColorOxfordBlue: 'Blu Oxford',
  optColorBulgarianRose: 'Rosa bulgaro',
  optColorDarkGreen: 'Verde scuro',
  optColorChromeYellow: 'Giallo cromo',
  labelTempColorSource: 'Colore icona meteo basato su',
  optRealfeel: 'RealFeel (percezione vento/caldo)',
  optActual: 'Temperatura reale',
  labelTempColorScale: 'Scala delle fasce di colore (temperatura)',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Unità',
  labelTempUnit: 'Unità di temperatura',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Unità velocità vento',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Unità precipitazioni',
  optInches: 'Pollici',
  optMillimeters: 'Millimetri',
  headingAdvanced: 'Avanzate',
  labelAllowSweep: 'Consenti test di scorrimento',
  headingCredits: 'Crediti',
  textCredits: CREDITS_TEXT,
  submitSave: 'Salva',
};

const PT: Strings = {
  appTitle: 'Configuração do Moderne',
  langLabel: 'Idioma',
  headingFeatures: 'Funcionalidades',
  labelTapTimeout: 'Tempo limite de toque/agitar',
  opt3s: '3 segundos',
  opt5s: '5 segundos',
  opt10s: '10 segundos',
  labelAaTime: 'Fonte da hora suavizada',
  labelAaDate: 'Fonte da data suavizada',
  labelAaWind: 'Fonte grande do clima suavizada',
  labelAaTemp: 'Fonte pequena do clima suavizada',
  labelRingLayout: 'Layout do anel do clima',
  optRingIconsOut: 'Ícones fora, temp./chuva % dentro',
  optRingTextOut: 'Temp./chuva % fora, ícones dentro',
  optRingIconsOnly: 'Somente ícones',
  optRingTextOnly: 'Somente temp./chuva %',
  labelRingTextTint: 'Texto do anel combina com a cor do ícone',
  headingWeatherInfo: 'Detalhes do clima',
  textWeatherInfo:
    'Até 4 são exibidos de uma vez (6 se o layout do anel do clima acima '
    + 'estiver definido para uma opção "somente"), por ordem de importância: '
    + 'temperatura, sensação térmica, precipitação, vento, umidade, rajadas '
    + 'de vento, índice UV, qualidade do ar, passos, sono. Marcar mais do que o limite apenas '
    + 'adiciona opções de prioridade menor, que não aparecerão até que uma de '
    + 'prioridade maior seja desmarcada. Por padrão, apenas vento e umidade, '
    + 'o conteúdo original deste bloco.',
  labelInfoTemp: 'Temperatura',
  labelInfoFeelsLike: 'Sensação térmica',
  labelInfoPrecip: 'Precipitação (atual)',
  labelInfoWind: 'Vento',
  labelInfoHumidity: 'Umidade',
  labelInfoGust: 'Rajadas de vento',
  labelInfoUv: 'Índice UV',
  labelInfoAqi: 'Qualidade do ar (AQI)',
  labelInfoSteps: 'Passos',
  labelInfoSleep: 'Sono',
  headingDateTime: 'Data e hora',
  labelDateFormat: 'Formato de data',
  optDateDdMonYyyy: 'Dia mês ano (08 ago 2026)',
  optDateDowDdMon: 'Dia semana, dia mês (qua 08 ago)',
  labelPadDay: 'Zero à esquerda no dia (08 em vez de 8)',
  labelPadHour: 'Zero à esquerda na hora (08:30 em vez de 8:30)',
  textAmPm:
    'Sem efeito no modo 24 horas. Duas opções independentes, não uma só: '
    + 'ativar apenas uma (ex.: Mostrar AM) exibe essa marca só nessa metade do '
    + 'dia, então sua ausência indica a outra metade.',
  labelShowAm: 'Mostrar AM',
  labelShowPm: 'Mostrar PM',
  headingFitness: 'Atividade física',
  textFitness:
    'Passos e sono, via HealthService, como uma linha compacta abaixo da '
    + 'data: um ícone de pegada para os passos, uma lua crescente para o '
    + 'sono. Alternâncias independentes -- uma, ambas ou nenhuma.',
  labelShowSteps: 'Mostrar passos',
  labelShowSleep: 'Mostrar sono',
  labelHealthIntensity: 'Intensidade da linha de saúde',
  optHealthIntensity100: '100% (intensidade total)',
  labelBatteryScale: 'Escala da barra de bateria',
  textBatteryScale:
    'A barra sob a hora mostra uma fração deste limite, não a porcentagem '
    + 'real da bateria: permanece cheia enquanto a carga estiver acima do '
    + 'valor escolhido e depois se esvazia proporcionalmente abaixo dele. '
    + 'Por exemplo, em 50%, uma carga real de 25% mostra uma barra meio '
    + 'cheia. 100% mostra a carga real diretamente, sem escala.',
  optBatteryScale100: '100% (corresponde à carga real)',
  headingCalendar: 'Calendário',
  labelShowNextAppt: 'Mostrar próximo compromisso',
  textCalendarUrl:
    'Cole abaixo a URL privada de assinatura iCal/ICS do seu calendário '
    + '(Google Agenda: Configurações > sua agenda > Integrar agenda > '
    + 'Endereço secreto no formato iCal). Essa URL concede acesso de leitura '
    + 'a todo o seu calendário a quem a tiver -- trate-a como uma senha. '
    + 'Deixe em branco para ver um lembrete de configuração no lugar.',
  labelCalendarUrl: 'URL ICS do calendário',
  labelReminderIntensity: 'Intensidade do texto do lembrete',
  optIntensity100: '100% (igual ao texto da data)',
  optIntensity75: '75%',
  optIntensity50: '50%',
  optIntensity25: '25%',
  labelHideApptTitle: 'Ocultar título do compromisso (mostrar só a hora)',
  headingMoon: 'Lua',
  labelMoonDisplay: 'Ícone da lua',
  optMoonAlways: 'Sempre (esmaecido quando não visível)',
  optMoonNight: 'Somente à noite (oculto de dia)',
  optMoonVisible: 'Somente quando visível (sem prévia esmaecida)',
  optMoonNever: 'Nunca',
  headingColors: 'Cores',
  textColorsOnly: 'Disponível apenas em relógios coloridos.',
  labelColorBgDay: 'Cor de fundo (dia)',
  labelColorBgNight: 'Cor de fundo (noite)',
  optColorBlack: 'Preto',
  optColorWhite: 'Branco',
  optColorOxfordBlue: 'Azul Oxford',
  optColorBulgarianRose: 'Rosa búlgaro',
  optColorDarkGreen: 'Verde escuro',
  optColorChromeYellow: 'Amarelo cromo',
  labelTempColorSource: 'Cor do ícone do clima baseada em',
  optRealfeel: 'RealFeel (sensação térmica de vento/calor)',
  optActual: 'Temperatura real',
  labelTempColorScale: 'Escala das faixas de cor (temperatura)',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Unidades',
  labelTempUnit: 'Unidade de temperatura',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Unidade de velocidade do vento',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Unidade de precipitação',
  optInches: 'Polegadas',
  optMillimeters: 'Milímetros',
  headingAdvanced: 'Avançado',
  labelAllowSweep: 'Permitir testes de varredura',
  headingCredits: 'Créditos',
  textCredits: CREDITS_TEXT,
  submitSave: 'Salvar',
};

const NL: Strings = {
  appTitle: 'Moderne-configuratie',
  langLabel: 'Taal',
  headingFeatures: 'Functies',
  labelTapTimeout: 'Tik-/schudtimeout',
  opt3s: '3 seconden',
  opt5s: '5 seconden',
  opt10s: '10 seconden',
  labelAaTime: 'Anti-aliased tijdlettertype',
  labelAaDate: 'Anti-aliased datumlettertype',
  labelAaWind: 'Anti-aliased groot weerlettertype',
  labelAaTemp: 'Anti-aliased klein weerlettertype',
  labelRingLayout: 'Indeling weerring',
  optRingIconsOut: 'Iconen buiten, temp./regen % binnen',
  optRingTextOut: 'Temp./regen % buiten, iconen binnen',
  optRingIconsOnly: 'Alleen iconen',
  optRingTextOnly: 'Alleen temp./regen %',
  labelRingTextTint: 'Ringtekst volgt iconkleur',
  headingWeatherInfo: 'Weerdetails',
  textWeatherInfo:
    'Er worden er tot 4 tegelijk getoond (6 als de weerring-indeling '
    + 'hierboven op een "alleen"-optie staat), op volgorde van belang: '
    + 'temperatuur, gevoelstemperatuur, neerslag, wind, luchtvochtigheid, '
    + 'windstoten, UV-index, luchtkwaliteit, stappen, slaap. Meer dan de limiet aanvinken '
    + 'voegt alleen opties met lagere prioriteit toe, die pas verschijnen als '
    + 'een hogere wordt uitgevinkt. Standaard alleen wind en luchtvochtigheid, '
    + 'de oorspronkelijke inhoud van dit blok.',
  labelInfoTemp: 'Temperatuur',
  labelInfoFeelsLike: 'Gevoelstemperatuur',
  labelInfoPrecip: 'Neerslag (huidig)',
  labelInfoWind: 'Wind',
  labelInfoHumidity: 'Luchtvochtigheid',
  labelInfoGust: 'Windstoten',
  labelInfoUv: 'UV-index',
  labelInfoAqi: 'Luchtkwaliteit (AQI)',
  labelInfoSteps: 'Stappen',
  labelInfoSleep: 'Slaap',
  headingDateTime: 'Datum en tijd',
  labelDateFormat: 'Datumnotatie',
  optDateDdMonYyyy: 'Dag maand jaar (08 aug 2026)',
  optDateDowDdMon: 'Weekdag dag maand (wo 08 aug)',
  labelPadDay: 'Dag met voorloopnul (08 i.p.v. 8)',
  labelPadHour: 'Uur met voorloopnul (08:30 i.p.v. 8:30)',
  textAmPm:
    'Geen effect in 24-uursmodus. Twee onafhankelijke schakelaars, geen '
    + 'enkele optie: alleen één inschakelen (bijv. Toon AM) laat die markering '
    + 'alleen in die helft van de dag zien, dus de afwezigheid ervan geeft de '
    + 'andere helft aan.',
  labelShowAm: 'Toon AM',
  labelShowPm: 'Toon PM',
  headingFitness: 'Fitness',
  textFitness:
    'Stappen en slaap, via HealthService, als compacte regel onder de '
    + 'datum: een voetafdruk-icoon voor stappen, een wassende maan voor '
    + 'slaap. Onafhankelijke schakelaars -- een van beide, allebei, of geen '
    + 'van beide.',
  labelShowSteps: 'Stappen tonen',
  labelShowSleep: 'Slaap tonen',
  labelHealthIntensity: 'Intensiteit van de gezondheidsregel',
  optHealthIntensity100: '100% (volledige intensiteit)',
  labelBatteryScale: 'Schaal van de batterijbalk',
  textBatteryScale:
    'De balk onder de tijd toont een fractie van deze drempel, niet het '
    + 'werkelijke batterijpercentage -- hij blijft vol zolang de lading '
    + 'boven de gekozen waarde ligt, en loopt daaronder proportioneel '
    + 'leeg. Bijvoorbeeld: bij 50% toont een werkelijke lading van 25% '
    + 'een halfvolle balk. 100% toont de werkelijke lading rechtstreeks, '
    + 'ongeschaald.',
  optBatteryScale100: '100% (komt overeen met de werkelijke lading)',
  headingCalendar: 'Agenda',
  labelShowNextAppt: 'Volgende afspraak tonen',
  textCalendarUrl:
    'Plak hieronder de privé iCal/ICS-abonnements-URL van je agenda (Google '
    + 'Agenda: Instellingen > jouw agenda > Agenda integreren > Geheim adres '
    + 'in iCal-indeling). Deze URL geeft leestoegang tot je hele agenda aan '
    + 'iedereen die hem heeft -- behandel hem als een wachtwoord. Laat leeg '
    + 'om in plaats daarvan een herinnering te zien om dit in te stellen.',
  labelCalendarUrl: 'ICS-URL van agenda',
  labelReminderIntensity: 'Tekstintensiteit van herinnering',
  optIntensity100: '100% (zelfde als datumtekst)',
  optIntensity75: '75%',
  optIntensity50: '50%',
  optIntensity25: '25%',
  labelHideApptTitle: 'Titel van afspraak verbergen (alleen tijd tonen)',
  headingMoon: 'Maan',
  labelMoonDisplay: 'Maanicoon',
  optMoonAlways: 'Altijd (gedimd wanneer niet zichtbaar)',
  optMoonNight: 'Alleen ’s nachts (overdag verborgen)',
  optMoonVisible: 'Alleen zichtbaar (geen gedimde voorvertoning)',
  optMoonNever: 'Nooit',
  headingColors: 'Kleuren',
  textColorsOnly: 'Alleen beschikbaar op horloges met kleurenscherm.',
  labelColorBgDay: 'Achtergrondkleur (dag)',
  labelColorBgNight: 'Achtergrondkleur (nacht)',
  optColorBlack: 'Zwart',
  optColorWhite: 'Wit',
  optColorOxfordBlue: 'Oxfordblauw',
  optColorBulgarianRose: 'Bulgaars roze',
  optColorDarkGreen: 'Donkergroen',
  optColorChromeYellow: 'Chroomgeel',
  labelTempColorSource: 'Kleur weericoon gebaseerd op',
  optRealfeel: 'RealFeel (windchill/hitte-index)',
  optActual: 'Werkelijke temperatuur',
  labelTempColorScale: 'Schaal van de temperatuurkleurbereiken',
  optTempColorScaleF: 'Fahrenheit (25/35/50/65/75/85/95°F)',
  optTempColorScaleC: 'Celsius (-5/0/10/20/25/30/35°C)',
  headingUnits: 'Eenheden',
  labelTempUnit: 'Temperatuureenheid',
  optCelsius: 'Celsius',
  optFahrenheit: 'Fahrenheit',
  labelWindUnit: 'Windsnelheidseenheid',
  optMph: 'MPH',
  optKph: 'KPH',
  labelPrecipUnit: 'Neerslageenheid',
  optInches: 'Inch',
  optMillimeters: 'Millimeter',
  headingAdvanced: 'Geavanceerd',
  labelAllowSweep: 'Sweep-tests toestaan',
  headingCredits: 'Met dank aan',
  textCredits: CREDITS_TEXT,
  submitSave: 'Opslaan',
};

const STRINGS: Record<LangCode, Strings> = { EN, ES, FR, DE, IT, PT, NL };

// Native names, always shown in their own language regardless of the
// currently active one -- standard i18n convention (a French speaker
// looking for their language needs to recognize "Français", not
// whatever "French" translates to in whatever language currently happens
// to be selected).
const LANG_NATIVE_NAMES: Record<LangCode, string> = {
  EN: 'English',
  ES: 'Español',
  FR: 'Français',
  DE: 'Deutsch',
  IT: 'Italiano',
  PT: 'Português',
  NL: 'Nederlands',
};

/**
 * Same `clay-settings` localStorage cache index.ts's readClaySettings()
 * already reads (see that function's own comment for how this was
 * confirmed, by reading @rebble/clay's own source) -- read here too, one
 * key, so a returning user's already-saved language choice is respected
 * on this page's *next* load, not just fresh navigator.language guesses
 * every time.
 */
const readSavedLanguage = (): LangCode | null => {
  try {
    const saved = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    const lang = saved.CONFIG_LANGUAGE;
    return (SUPPORTED_LANGS as readonly string[]).includes(lang) ? (lang as LangCode) : null;
  } catch {
    return null;
  }
};

const detectDefaultLanguage = (): LangCode => {
  const saved = readSavedLanguage();
  if (saved) return saved;
  try {
    // navigator.language is e.g. "fr-FR", "de", "pt-BR" -- the first two
    // characters are the ISO 639-1 language code regardless of region.
    const nav = (typeof navigator !== 'undefined' && navigator.language) || '';
    const prefix = nav.slice(0, 2).toUpperCase();
    if ((SUPPORTED_LANGS as readonly string[]).includes(prefix)) return prefix as LangCode;
  } catch {
    // Fall through to the EN default below -- no navigator, or a locale
    // string in an unexpected shape.
  }
  return 'EN';
};

const ACTIVE_LANG: LangCode = detectDefaultLanguage();
const t = (key: keyof Strings): string => STRINGS[ACTIVE_LANG][key];

const CLAY_SCHEMA = [
  { type: 'heading', defaultValue: t('appTitle') },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('langLabel') },
      {
        type: 'select',
        messageKey: 'CONFIG_LANGUAGE',
        defaultValue: ACTIVE_LANG,
        label: t('langLabel'),
        options: SUPPORTED_LANGS.map((code) => ({ label: LANG_NATIVE_NAMES[code], value: code })),
      },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingFeatures') },
      {
        type: 'select',
        messageKey: 'CONFIG_TAP_TIMEOUT',
        defaultValue: '5',
        label: t('labelTapTimeout'),
        options: [
          { label: t('opt3s'), value: '3' },
          { label: t('opt5s'), value: '5' },
          { label: t('opt10s'), value: '10' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_AA_TIME',
        label: t('labelAaTime'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_AA_DATE',
        label: t('labelAaDate'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_AA_WIND',
        label: t('labelAaWind'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_AA_TEMP',
        label: t('labelAaTemp'),
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'CONFIG_RING_LAYOUT',
        defaultValue: 'TEXT_OUT',
        label: t('labelRingLayout'),
        options: [
          { label: t('optRingIconsOut'), value: 'ICONS_OUT' },
          { label: t('optRingTextOut'), value: 'TEXT_OUT' },
          { label: t('optRingIconsOnly'), value: 'ICONS_ONLY' },
          { label: t('optRingTextOnly'), value: 'TEXT_ONLY' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_RING_TEXT_TINT',
        label: t('labelRingTextTint'),
        defaultValue: true
      },
      {
        type: 'text',
        defaultValue: t('textBatteryScale')
      },
      {
        type: 'select',
        messageKey: 'CONFIG_BATTERY_SCALE',
        defaultValue: '100',
        label: t('labelBatteryScale'),
        options: [
          { label: t('optBatteryScale100'), value: '100' },
          { label: t('optIntensity75'), value: '75' },
          { label: t('optIntensity50'), value: '50' },
          { label: t('optIntensity25'), value: '25' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingWeatherInfo') },
      {
        type: 'text',
        defaultValue: t('textWeatherInfo')
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_TEMP',
        label: t('labelInfoTemp'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_FEELS_LIKE',
        label: t('labelInfoFeelsLike'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_PRECIP',
        label: t('labelInfoPrecip'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_WIND',
        label: t('labelInfoWind'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_HUMIDITY',
        label: t('labelInfoHumidity'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_GUST',
        label: t('labelInfoGust'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_UV',
        label: t('labelInfoUv'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_AQI',
        label: t('labelInfoAqi'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_STEPS',
        label: t('labelInfoSteps'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_INFO_SLEEP',
        label: t('labelInfoSleep'),
        defaultValue: false
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingDateTime') },
      {
        type: 'select',
        messageKey: 'CONFIG_DATE_FORMAT',
        defaultValue: 'DOW_DD_MON',
        label: t('labelDateFormat'),
        options: [
          { label: t('optDateDdMonYyyy'), value: 'DD_MON_YYYY' },
          { label: t('optDateDowDdMon'), value: 'DOW_DD_MON' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_PAD_DAY',
        label: t('labelPadDay'),
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_PAD_HOUR',
        label: t('labelPadHour'),
        defaultValue: true
      },
      {
        type: 'text',
        defaultValue: t('textAmPm')
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_SHOW_AM',
        label: t('labelShowAm'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_SHOW_PM',
        label: t('labelShowPm'),
        defaultValue: false
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingFitness') },
      {
        type: 'text',
        defaultValue: t('textFitness')
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_SHOW_STEPS',
        label: t('labelShowSteps'),
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_SHOW_SLEEP',
        label: t('labelShowSleep'),
        defaultValue: false
      },
      {
        type: 'select',
        messageKey: 'CONFIG_HEALTH_INTENSITY',
        defaultValue: '100',
        label: t('labelHealthIntensity'),
        options: [
          { label: t('optHealthIntensity100'), value: '100' },
          { label: t('optIntensity75'), value: '75' },
          { label: t('optIntensity50'), value: '50' },
          { label: t('optIntensity25'), value: '25' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingCalendar') },
      {
        type: 'toggle',
        messageKey: 'CONFIG_SHOW_NEXT_APPT',
        label: t('labelShowNextAppt'),
        defaultValue: false
      },
      {
        type: 'text',
        defaultValue: t('textCalendarUrl')
      },
      {
        type: 'input',
        messageKey: 'CONFIG_CALENDAR_ICS_URL',
        defaultValue: '',
        label: t('labelCalendarUrl'),
        attributes: {
          type: 'url',
          placeholder: 'https://calendar.google.com/calendar/ical/.../basic.ics'
        }
      },
      {
        type: 'select',
        messageKey: 'CONFIG_REMINDER_INTENSITY',
        defaultValue: '25',
        label: t('labelReminderIntensity'),
        options: [
          { label: t('optIntensity100'), value: '100' },
          { label: t('optIntensity75'), value: '75' },
          { label: t('optIntensity50'), value: '50' },
          { label: t('optIntensity25'), value: '25' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'CONFIG_HIDE_APPT_TITLE',
        label: t('labelHideApptTitle'),
        defaultValue: false
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingMoon') },
      {
        type: 'select',
        messageKey: 'CONFIG_MOON_DISPLAY',
        defaultValue: 'NIGHT',
        label: t('labelMoonDisplay'),
        options: [
          { label: t('optMoonAlways'), value: 'ALWAYS' },
          { label: t('optMoonNight'), value: 'NIGHT' },
          { label: t('optMoonVisible'), value: 'VISIBLE' },
          { label: t('optMoonNever'), value: 'NEVER' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingColors') },
      { type: 'text', defaultValue: t('textColorsOnly') },
      {
        type: 'select',
        messageKey: 'CONFIG_COLOR_BG',
        defaultValue: 'GColorOxfordBlue',
        label: t('labelColorBgDay'),
        options: [
          { label: t('optColorBlack'), value: 'GColorBlack' },
          { label: t('optColorWhite'), value: 'GColorWhite' },
          { label: t('optColorOxfordBlue'), value: 'GColorOxfordBlue' },
          { label: t('optColorBulgarianRose'), value: 'GColorBulgarianRose' },
          { label: t('optColorDarkGreen'), value: 'GColorDarkGreen' },
          { label: t('optColorChromeYellow'), value: 'GColorChromeYellow' }
        ]
      },
      {
        type: 'select',
        messageKey: 'CONFIG_COLOR_BG_NIGHT',
        defaultValue: 'GColorBlack',
        label: t('labelColorBgNight'),
        options: [
          { label: t('optColorBlack'), value: 'GColorBlack' },
          { label: t('optColorWhite'), value: 'GColorWhite' },
          { label: t('optColorOxfordBlue'), value: 'GColorOxfordBlue' },
          { label: t('optColorBulgarianRose'), value: 'GColorBulgarianRose' },
          { label: t('optColorDarkGreen'), value: 'GColorDarkGreen' },
          { label: t('optColorChromeYellow'), value: 'GColorChromeYellow' }
        ]
      },
      {
        type: 'select',
        messageKey: 'CONFIG_TEMP_COLOR_SOURCE',
        defaultValue: 'REALFEEL',
        label: t('labelTempColorSource'),
        options: [
          { label: t('optRealfeel'), value: 'REALFEEL' },
          { label: t('optActual'), value: 'ACTUAL' }
        ]
      },
      {
        type: 'select',
        messageKey: 'CONFIG_TEMP_COLOR_SCALE',
        defaultValue: 'FAHRENHEIT',
        label: t('labelTempColorScale'),
        options: [
          { label: t('optTempColorScaleF'), value: 'FAHRENHEIT' },
          { label: t('optTempColorScaleC'), value: 'CELSIUS' }
        ]
      },
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingUnits') },
      {
        type: 'select',
        messageKey: 'CONFIG_TEMP_UNIT',
        defaultValue: 'F',
        label: t('labelTempUnit'),
        options: [
          { label: t('optCelsius'), value: 'C' },
          { label: t('optFahrenheit'), value: 'F' }
        ]
      },
      {
        type: 'select',
        messageKey: 'CONFIG_WIND_UNIT',
        defaultValue: 'MPH',
        label: t('labelWindUnit'),
        options: [
          { label: t('optMph'), value: 'MPH' },
          { label: t('optKph'), value: 'KPH' }
        ]
      },
      {
        type: 'select',
        messageKey: 'CONFIG_PRECIP_UNIT',
        defaultValue: 'IN',
        label: t('labelPrecipUnit'),
        options: [
          { label: t('optInches'), value: 'IN' },
          { label: t('optMillimeters'), value: 'MM' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingAdvanced') },
      {
        type: 'toggle',
        messageKey: 'CONFIG_ALLOW_SWEEP',
        label: t('labelAllowSweep'),
        defaultValue: false
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t('headingCredits') },
      { type: 'text', defaultValue: t('textCredits') }
    ]
  },
  { type: 'submit', defaultValue: t('submitSave') }
];

export const setupClay = () => {
  // @ts-ignore
  const ClayCtor: ClayFactory = require('@rebble/clay');
  new ClayCtor(CLAY_SCHEMA);
};
