#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace stable_ui {
struct preferences { bool russian=true; int theme=0; bool animations=true; float motion=1.f; float menu_scale=1.f; };
inline preferences prefs;
inline std::filesystem::path preferences_path(){
#ifdef _WIN32
 wchar_t root[MAX_PATH]{};
 if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,root)))return std::filesystem::path(root)/L"OSUBAND"/L"Stable"/L"ui.ini";
#endif
 return {};
}
inline void load_preferences(){auto path=preferences_path();if(path.empty())return;std::ifstream f(path);int ru=1,theme=0,motion=1;float speed=1,scale=1;
 if(f>>ru>>theme>>motion>>speed){if(!(f>>scale))scale=1; prefs={ru!=0,std::clamp(theme,0,2),motion!=0,std::isfinite(speed)?std::clamp(speed,0.25f,2.f):1.f,std::isfinite(scale)?std::clamp(scale,0.75f,1.35f):1.f};}}
inline void save_preferences(){auto path=preferences_path();if(path.empty())return;std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return;
 std::ofstream f(path);f<<prefs.russian<<' '<<prefs.theme<<' '<<prefs.animations<<' '<<prefs.motion<<' '<<prefs.menu_scale;}
inline const char* tr(const char* s){
 if(!prefs.russian)return s;
 static const std::unordered_map<std::string,const char*> words={
 {"Play","Игра"},{"Config","Конфиг"},{"Settings","Настройки"},{"Modules","Модули"},{"Module settings","НАСТРОЙКИ МОДУЛЯ"},
 {"Aim Assist","Aim Assist"},{"Relax","Relax"},{"Replay","Replay"},{"Cursor correction","Коррекция курсора"},{"Automatic key timing","Автоматический тайминг нажатий"},{"Your .osr playback","Воспроизведение .osr"},{"Enable module","Включить модуль"},
 {"Horizontal strength","Коррекция по X"},{"Vertical strength","Коррекция по Y"},{"Smoothing","Плавность движения"},{"Approach window","Время захвата"},{"Distance falloff","Ослабление по дистанции"},{"Freeze smoothing","Плавность остановки"},{"Tablet mode","Режим планшета"},{"Ignore sliders","Не трогать слайдеры"},{"Limit correction","Ограничение коррекции"},
 {"Timing variation / UR","Разброс тайминга / UR"},{"Manual timing offset","Смещение нажатий"},{"Prefer single tap","Предпочитать одну клавишу"},{"Single tap BPM ceiling","Порог чередования BPM"},{"K1 hold","Удержание K1"},{"K2 hold","Удержание K2"},{"K1 spread","Разброс K1"},{"K2 spread","Разброс K2"},
 {"REPLAY FILE","ФАЙЛ ПОВТОРА"},{"Browse","Выбрать файл"},{"Load replay","Загрузить повтор"},{"Playback mode","Режим повтора"},{"Full playback","Курсор и клавиши"},{"Cursor only","Только курсор"},{"Keys only","Только клавиши"},{"Replay must match the selected beatmap.","Повтор должен соответствовать карте."},{"No replay loaded","Повтор не выбран"},
 {"Refresh","Обновить"},{"Publish your config","Опубликовать конфиг"},{"Name","Название"},{"Description","Описание"},{"Save private keeps it only in your account.","Личный конфиг виден только тебе."},{"Submit for review asks an admin to publish it","Проверка нужна, чтобы администратор опубликовал конфиг"},{"for everyone using the Stable build.","для всех пользователей Stable."},{"Save private","Сохранить личным"},{"Submit for review","Отправить на проверку"},{"Loading...","Загрузка…"},{"No configs yet","Здесь пока нет конфигов"},{"Administrator","Администратор"},{"Player","Игрок"},{"Installed","Установлен"},{"Apply","Применить"},{"Remove","Удалить у себя"},{"Install","Установить"},{"Select a config","Выбери конфиг"},{"Cloud only · changes apply between maps","Только облако · применяется между картами"},
 {"Language","Язык"},{"Theme","Тема"},{"Animations","Анимации"},{"Animation speed","Скорость анимаций"},{"Ocean","Синяя"},{"Graphite","Графит"},{"Light","Светлая"},{"Menu scale","Масштаб меню"},{"Gameplay keys","Клавиши игры"},{"Menu key","Клавиша меню"},{"Press a key...","Нажми клавишу…"},{"Watermark","Водяной знак"},{"Exclude menu from capture","Скрывать меню при захвате"},{"UNINJECT","ВЫГРУЗИТЬ"},
 {"Pause modules  F8","Пауза функций · F8"},{"Resume modules","Продолжить"},{"MODULES PAUSED","ФУНКЦИИ НА ПАУЗЕ"},{"Ready","Готово"},{"Settings applied.","Настройки применены."},{"Config applied","Конфиг применён"}
 };
 auto it=words.find(s);return it==words.end()?s:it->second;
}
}
