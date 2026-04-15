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
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return { YomkResponse::eErr, "YString is empty" };
    }
    nlohmann::json tempSettings;
    try
    {
        std::ifstream file(yStr->d);
        if (!file.is_open()) 
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "cannot open file, please check file path" << std::endl;
            return { YomkResponse::eErr, "cannot open file, please check file path" };
        }
        tempSettings = nlohmann::json::parse(file);
        file.close();
    }
    catch (const std::exception &e)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] load settings failed: " << e.what() << std::endl;
        return { YomkResponse::eErr, e.what() };
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_settingsMutex);
        m_settings = std::move(tempSettings);
    }

    return { YomkResponse::eOk };
}

YomkResponse YomkSettings::save(YomkPkgPtr pkg)
{
    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return { YomkResponse::eErr, "YString is empty" };
    }
    std::string jsonData;
    {
        std::unique_lock<std::shared_mutex> lock(m_settingsMutex);
        jsonData = m_settings.dump(4);
    }

    try
    {
        std::ofstream file(yStr->d);
        if (!file.is_open()) 
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "cannot open file, please check file path" << std::endl;
            return { YomkResponse::eErr, "cannot open file, please check file path" };
        }
    
        file << jsonData;
        file.flush();
        
        if (file.fail())
        {
            file.close();
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "write failed, please check file path" << std::endl;
            return { YomkResponse::eErr, "write failed, please check file path" };
        }

        file.close();
    }
    catch (const std::exception &e)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] save settings failed: " << e.what() << std::endl;
        return { YomkResponse::eErr, e.what() };
    }
    return { YomkResponse::eOk };
}

YomkResponse YomkSettings::get(YomkPkgPtr pkg)
{
    std::shared_lock<std::shared_mutex> lock(m_settingsMutex);

    YomkUnPackPkgresponse(pkg, "YString", YString, yStr);
    if(!yStr)
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YString is empty, please check YString" << std::endl;
        return { YomkResponse::eErr, "YString is empty" };
    }
    if(!m_settings.contains(yStr->d))
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
        return { YomkResponse::eErr, "setting not found" };
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
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "array is empty, set " << yStr->d << " failed, please check array." << std::endl;
            return { YomkResponse::eErr, "array is empty" };
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
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "boolean array is empty, set " << yStr->d << " failed, please check array." << std::endl;
                return { YomkResponse::eErr, "boolean array is empty" };
            }
            catch(const std::exception& e)
            {
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "set " << yStr->d << " failed: " << e.what() << std::endl;
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
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "int array is empty, set " << yStr->d << " failed, please check array." << std::endl;
                return { YomkResponse::eErr, "int array is empty" };
            }
            catch(const std::exception& e)
            {
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "set " << yStr->d << " failed: " << e.what() << std::endl;
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
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "double array is empty, set " << yStr->d << " failed, please check array." << std::endl;
                return { YomkResponse::eErr, "double array is empty" };
            }
            catch(const std::exception& e)
            {
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "set " << yStr->d << " failed: " << e.what() << std::endl;
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
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "string array is empty, set " << yStr->d << " failed, please check array." << std::endl;
                return { YomkResponse::eErr, " string array is empty. " };
            }
            catch(const std::exception& e)
            {
                std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "set " << yStr->d << " failed: " << e.what() << std::endl;
                return { YomkResponse::eErr, e.what() };
            }
        }
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "unknown array type, set " << yStr->d << " failed, please check array." << std::endl;
            return { YomkResponse::eErr, "unknown array type" };
        }
    }
    else
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "unknown type, set " << yStr->d << " failed, please check type." << std::endl;
        return { YomkResponse::eErr, "unknown type" };
    }
}

YomkResponse YomkSettings::set(YomkPkgPtr pkg)
{
    std::unique_lock<std::shared_mutex> lock(m_settingsMutex);
    
    if(pkg->name() == "YSettingString")
    {
        YomkUnPackPkgresponse(pkg, "YSettingString", YSettingString, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingString is empty, please check YSettingString" << std::endl;
            return { YomkResponse::eErr, "YSettingString is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::string)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingDouble")
    {
        YomkUnPackPkgresponse(pkg, "YSettingDouble", YSettingDouble, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingDouble is empty, please check YSettingDouble" << std::endl;
            return { YomkResponse::eErr, "YSettingDouble is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_float)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingInt")
    {
        YomkUnPackPkgresponse(pkg, "YSettingInt", YSettingInt, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingInt is empty, please check YSettingInt" << std::endl;
            return { YomkResponse::eErr, "YSettingInt is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_unsigned || 
           m_settings[ySetting->m_key].type() == nlohmann::json::value_t::number_integer)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingBool")
    {
        YomkUnPackPkgresponse(pkg, "YSettingBool", YSettingBool, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingBool is empty, please check YSettingBool" << std::endl;
            return { YomkResponse::eErr, "YSettingBool is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::boolean)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingBoolArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingBoolArray", YSettingBoolArray, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingBoolArray is empty, please check YSettingBoolArray" << std::endl;
            return { YomkResponse::eErr, "YSettingBoolArray is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if (pkg->name() == "YSettingIntArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingIntArray", YSettingIntArray, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingIntArray is empty, please check YSettingIntArray" << std::endl;
            return { YomkResponse::eErr, "YSettingIntArray is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingDoubleArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingDoubleArray", YSettingDoubleArray, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingDoubleArray is empty, please check YSettingDoubleArray" << std::endl;
            return { YomkResponse::eErr, "YSettingDoubleArray is empty" };
        }
        
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else if(pkg->name() == "YSettingStringArray")
    {
        YomkUnPackPkgresponse(pkg, "YSettingStringArray", YSettingStringArray, ySetting);
        if(!ySetting)
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "YSettingStringArray is empty, please check YSettingStringArray" << std::endl;
            return { YomkResponse::eErr, "YSettingStringArray is empty" };
        }
        if(!m_settings.contains(ySetting->m_key))
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "setting not found, please check setting name" << std::endl;
            return { YomkResponse::eErr, "setting not found" };
        }

        if(m_settings[ySetting->m_key].type() == nlohmann::json::value_t::array)
        {
            m_settings[ySetting->m_key] = ySetting->d;
            return { YomkResponse::eOk };
        } 
        else
        {
            std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "type not match, please check type" << std::endl;
            return { YomkResponse::eErr, "type not match" };
        }
    }
    else
    {
        std::cout << " [Yomk] [" << __FILE__ << ":" << __LINE__ << "] [" << __func__ << "] " << "unknown type, please check type" << std::endl;
        return { YomkResponse::eErr, "unknown type" };
    }

    return { YomkResponse::eOk };
}
