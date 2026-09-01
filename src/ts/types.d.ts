/** Weather response format */
export type WeatherApiResponse = {
  current: {
    temperature_2m: number;
    weather_code: number;
    relative_humidity_2m: number;
    wind_speed_10m: number;
    wind_direction_10m: number;
    apparent_temperature: number;
    precipitation: number;
    wind_gusts_10m: number;
  };
  hourly: {
    time: string[];
    temperature_2m: number[];
    precipitation_probability: number[];
    weather_code: number[];
    apparent_temperature: number[];
    uv_index: number[];
  };
  daily: {
    sunrise: string[];
    sunset: string[];
  };
};

/** Open-Meteo Air Quality API response format (separate host from the main forecast API) */
export type AirQualityApiResponse = {
  current: {
    us_aqi: number;
  };
};
