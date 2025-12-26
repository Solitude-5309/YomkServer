#include "YomkSettings.h"
#include <iostream>
#include <fstream>

YomkSettings::YomkSettings(YomkServer *server)
    : YomkService(server)
{
    name("/YomkSettings");
}

int YomkSettings::init()
{
    YomkInstallFunc("/load", YomkSettings::load);
    YomkInstallFunc("/save", YomkSettings::save);
    YomkInstallFunc("/get", YomkSettings::get);
    YomkInstallFunc("/set", YomkSettings::set);
    return 0;
}

YomkRespond YomkSettings::load(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);

    try
    {
        std::ifstream file(yStr->d);
        if (!file.is_open()) return { YomkRespond::eErr, " [YomkSettings::load] cannot open file. " };
        m_settings = nlohmann::json::parse(file);
        file.close();
    }
    catch (const std::exception &e)
    {
        return { YomkRespond::eErr, e.what() };
    }

    return { YomkRespond::eOk };
}

YomkRespond YomkSettings::save(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);
    try
    {
        std::ofstream file(yStr->d);
        if (!file.is_open()) return { YomkRespond::eErr, " [YomkSettings::save] cannot open file. " };
        file << m_settings.dump(4);
        file.close();
    }
    catch (const std::exception &e)
    {
        return { YomkRespond::eErr, e.what() };
    }
    return { YomkRespond::eOk };
}

YomkRespond YomkSettings::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgRespond(pkg, "YString", YString, yStr);

    if(!m_settings.contains(yStr->d))
    {
        return { YomkRespond::eErr, " [YomkSettings::get] setting not found. " };
    }

    if(m_settings[yStr->d].type() == nlohmann::json::value_t::string)
    {
        std::shared_ptr<YSettingString> ySetting = std::make_shared<YSettingString>(yStr->d, m_settings[yStr->d].get<std::string>());
        return { YomkRespond::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::number_float)
    {
        std::shared_ptr<YSettingDouble> ySetting = std::make_shared<YSettingDouble>(yStr->d, m_settings[yStr->d].get<double>());
        return { YomkRespond::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::number_unsigned || 
            m_settings[yStr->d].type() == nlohmann::json::value_t::number_integer)
    {
        std::shared_ptr<YSettingInt> ySetting = std::make_shared<YSettingInt>(yStr->d, m_settings[yStr->d].get<int>());
        return { YomkRespond::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::boolean)
    {
        std::shared_ptr<YSettingBool> ySetting = std::make_shared<YSettingBool>(yStr->d, m_settings[yStr->d].get<bool>());
        return { YomkRespond::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::array)
    {
        if(m_settings[yStr->d].size() == 0)
        {
            return { YomkRespond::eErr, " array is empty. " };
        }

        if(m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::boolean)
        {
            try
            {
                std::vector<bool> boolArray = m_settings[yStr->d].get<std::vector<bool>>();
                if(!boolArray.empty())
                {
                    std::shared_ptr<YSettingBoolArray> ySetting = std::make_shared<YSettingBoolArray>(yStr->d, boolArray);
                    return { YomkRespond::eOk, "success", ySetting };
                }
                return { YomkRespond::eErr, " boolean array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkRespond::eErr, e.what() };
            }
        }
        else if(m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::number_unsigned || 
                m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::number_integer)
        {
            try
            {
                std::vector<int> intArray = m_settings[yStr->d].get<std::vector<int>>();
                if(!intArray.empty())
                {
                    std::shared_ptr<YSettingIntArray> ySetting = std::make_shared<YSettingIntArray>(yStr->d, intArray);
                    return { YomkRespond::eOk, "success", ySetting };
                }
                return { YomkRespond::eErr, " int array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkRespond::eErr, e.what() };
            }
        }
        else if(m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::number_float)
        {
            try
            {
                std::vector<double> doubleArray = m_settings[yStr->d].get<std::vector<double>>();
                if(!doubleArray.empty())
                {
                    std::shared_ptr<YSettingDoubleArray> ySetting = std::make_shared<YSettingDoubleArray>(yStr->d, doubleArray);
                    return { YomkRespond::eOk, "success", ySetting };
                }
                return { YomkRespond::eErr, " double array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkRespond::eErr, e.what() };
            }
        }
        else if(m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::string)
        {
            try
            {
                std::vector<std::string> stringArray = m_settings[yStr->d].get<std::vector<std::string>>();
                if(!stringArray.empty())
                {
                    std::shared_ptr<YSettingStringArray> ySetting = std::make_shared<YSettingStringArray>(yStr->d, stringArray);
                    return { YomkRespond::eOk, "success", ySetting };
                }
                return { YomkRespond::eErr, " string array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkRespond::eErr, e.what() };
            }
        }
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::get] unknown array type. " };
        }
    }
    else
    {
        return { YomkRespond::eErr, " [YomkSettings::get] unknown type. " };
    }
}

YomkRespond YomkSettings::set(YomkPkgPtr pkg)
{
    if(pkg->name() == "YSettingString")
    {
        YomkUnPackPkgRespond(pkg, "YSettingString", YSettingString, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::string)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingDouble")
    {
        YomkUnPackPkgRespond(pkg, "YSettingDouble", YSettingDouble, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_float)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingInt")
    {
        YomkUnPackPkgRespond(pkg, "YSettingInt", YSettingInt, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_unsigned || 
           m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_integer)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingBool")
    {
        YomkUnPackPkgRespond(pkg, "YSettingBool", YSettingBool, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::boolean)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingBoolArray")
    {
        YomkUnPackPkgRespond(pkg, "YSettingBoolArray", YSettingBoolArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if (pkg->name() == "YSettingIntArray")
    {
        YomkUnPackPkgRespond(pkg, "YSettingIntArray", YSettingIntArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingDoubleArray")
    {
        YomkUnPackPkgRespond(pkg, "YSettingDoubleArray", YSettingDoubleArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingStringArray")
    {
        YomkUnPackPkgRespond(pkg, "YSettingStringArray", YSettingStringArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkRespond::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkRespond::eOk };
        } 
        else
        {
            return { YomkRespond::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else
    {
        return { YomkRespond::eErr, " [YomkSettings::set] unknown type. " };
    }

    return { YomkRespond::eOk };
}
