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

YomkResponse YomkSettings::load(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);

    try
    {
        std::ifstream file(yStr->d);
        if (!file.is_open()) return { YomkResponse::eErr, " [YomkSettings::load] cannot open file. " };
        m_settings = nlohmann::json::parse(file);
        file.close();
    }
    catch (const std::exception &e)
    {
        return { YomkResponse::eErr, e.what() };
    }

    return { YomkResponse::eOk };
}

YomkResponse YomkSettings::save(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    try
    {
        std::ofstream file(yStr->d);
        if (!file.is_open()) return { YomkResponse::eErr, " [YomkSettings::save] cannot open file. " };
        file << m_settings.dump(4);
        file.close();
    }
    catch (const std::exception &e)
    {
        return { YomkResponse::eErr, e.what() };
    }
    return { YomkResponse::eOk };
}

YomkResponse YomkSettings::get(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);

    if(!m_settings.contains(yStr->d))
    {
        return { YomkResponse::eErr, " [YomkSettings::get] setting not found. " };
    }

    if(m_settings[yStr->d].type() == nlohmann::json::value_t::string)
    {
        std::shared_ptr<YSettingString> ySetting = std::make_shared<YSettingString>(yStr->d, m_settings[yStr->d].get<std::string>());
        return { YomkResponse::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::number_float)
    {
        std::shared_ptr<YSettingDouble> ySetting = std::make_shared<YSettingDouble>(yStr->d, m_settings[yStr->d].get<double>());
        return { YomkResponse::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::number_unsigned || 
            m_settings[yStr->d].type() == nlohmann::json::value_t::number_integer)
    {
        std::shared_ptr<YSettingInt> ySetting = std::make_shared<YSettingInt>(yStr->d, m_settings[yStr->d].get<int>());
        return { YomkResponse::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::boolean)
    {
        std::shared_ptr<YSettingBool> ySetting = std::make_shared<YSettingBool>(yStr->d, m_settings[yStr->d].get<bool>());
        return { YomkResponse::eOk, "success", ySetting };
    }
    else if(m_settings[yStr->d].type() == nlohmann::json::value_t::array)
    {
        if(m_settings[yStr->d].size() == 0)
        {
            return { YomkResponse::eErr, " array is empty. " };
        }

        if(m_settings[yStr->d].at(0).type() == nlohmann::json::value_t::boolean)
        {
            try
            {
                std::vector<bool> boolArray = m_settings[yStr->d].get<std::vector<bool>>();
                if(!boolArray.empty())
                {
                    std::shared_ptr<YSettingBoolArray> ySetting = std::make_shared<YSettingBoolArray>(yStr->d, boolArray);
                    return { YomkResponse::eOk, "success", ySetting };
                }
                return { YomkResponse::eErr, " boolean array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkResponse::eErr, e.what() };
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
                    return { YomkResponse::eOk, "success", ySetting };
                }
                return { YomkResponse::eErr, " int array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkResponse::eErr, e.what() };
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
                    return { YomkResponse::eOk, "success", ySetting };
                }
                return { YomkResponse::eErr, " double array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkResponse::eErr, e.what() };
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
                    return { YomkResponse::eOk, "success", ySetting };
                }
                return { YomkResponse::eErr, " string array is empty. " };
            }
            catch(const std::exception& e)
            {
                return { YomkResponse::eErr, e.what() };
            }
        }
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::get] unknown array type. " };
        }
    }
    else
    {
        return { YomkResponse::eErr, " [YomkSettings::get] unknown type. " };
    }
}

YomkResponse YomkSettings::set(YomkPkgPtr pkg)
{
    if(pkg->name() == "YSettingString")
    {
        YomkUnPackPkgresponse(pkg, "YSettingString", YSettingString, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::string)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingDouble")
    {
        YomkUnPackPkgresponse(pkg, "YSettingDouble", YSettingDouble, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_float)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingInt")
    {
        YomkUnPackPkgresponse(pkg, "YSettingInt", YSettingInt, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_unsigned || 
           m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_integer)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingBool")
    {
        YomkUnPackPkgresponse(pkg, "YSettingBool", YSettingBool, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::boolean)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingBoolArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingBoolArray", YSettingBoolArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if (pkg->name() == "YSettingIntArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingIntArray", YSettingIntArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingDoubleArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingDoubleArray", YSettingDoubleArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else if(pkg->name() == "YSettingStringArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingStringArray", YSettingStringArray, ySetting);
        if(!m_settings.contains(ySetting->m_key))
        {
            return { YomkResponse::eErr, " [YomkSettings::set] setting not found. " };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            return { YomkResponse::eErr, " [YomkSettings::set] type not match. " };
        }
    }
    else
    {
        return { YomkResponse::eErr, " [YomkSettings::set] unknown type. " };
    }

    return { YomkResponse::eOk };
}
