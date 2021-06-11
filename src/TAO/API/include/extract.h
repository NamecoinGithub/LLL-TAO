/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/
#pragma once

#include <TAO/API/types/exception.h>

#include <Util/include/json.h>

/* Global TAO namespace. */
namespace TAO::API
{
    /** ExtractAddress
     *
     *  Extract an address from incoming parameters to derive from name or address field.
     *
     *  @param[in] jParams The parameters to find address in.
     *  @param[in] strSuffix The suffix to append to end of parameter we are extracting.
     *  @param[in] strDefault The default value to revert to if failed to find in parameters.
     *
     *  @return The register address if valid.
     *
     **/
    uint256_t ExtractAddress(const encoding::json& jParams, const std::string& strSuffix = "", const std::string& strDefault = "");


    /** ExtractToken
     *
     *  Extract a token address from incoming parameters to derive from name or address field.
     *
     *  @param[in] jParams The parameters to find address in.
     *
     *  @return The register address if valid.
     *
     **/
    uint256_t ExtractToken(const encoding::json& jParams);


    /** ExtractAmount
     *
     *  Extract an amount value from either string or integer and convert to its final value.
     *
     *  @param[in] jParams The parameters to extract amount from.
     *  @param[in] nFigures The figures calculated from decimals.
     *
     *  @return The amount represented as whole integer value.
     *
     **/
    uint64_t ExtractAmount(const encoding::json& jParams, const uint64_t nFigures);


    /** ExtractList
     *
     *  Extracts the paramers applicable to a List API call in order to apply a filter/offset/limit to the result
     *
     *  @param[in] params The parameters passed into the request
     *  @param[out] strOrder The sort order to apply
     *  @param[out] nLimit The number of results to return
     *  @param[out] nOffset The offset to apply to the results
     *
     **/
    void ExtractList(const encoding::json& params, std::string &strOrder, uint32_t &nLimit, uint32_t &nOffset);


    /** ExtractInteger
     *
     *  Extracts an integer value from given input parameters.
     *
     *  @param[in] jParams The parameters that contain requesting value.
     *  @param[in] strKey The key that we are checking parameters for.
     *  @param[in] nLimit The numeric limits to bound this type to, that may be less than type's value.
     *
     *  @return The extracted integer value.
     *
     **/
    template<typename Type>
    Type ExtractInteger(const encoding::json& jParams, const char* strKey, const Type nLimit = std::numeric_limits<Type>::max())
    {
        /* Check for missing parameter. */
        if(jParams.find(strKey) != jParams.end())
        {
            /* Catch parsing exceptions. */
            try
            {
                /* Build our return value. */
                Type nRet = 0;

                /* Convert to value if in string form. */
                if(jParams[strKey].is_string())
                    nRet = std::stoull(jParams[strKey].get<std::string>());

                /* Grab value regularly if it is integer. */
                else if(jParams[strKey].is_number_integer())
                    nRet = jParams[strKey].get<Type>();

                /* Otherwise we have an invalid parameter. */
                else
                    throw APIException(-57, "Invalid Parameter [", strKey, "]");

                /* Check our numeric limits now. */
                if(nRet > nLimit)
                    throw APIException(-60, "[", strKey, "] out of range [", nLimit, "]");

                return nRet;
            }
            catch(const encoding::detail::exception& e) { throw APIException(-57, "Invalid Parameter [", strKey, "]");           }
            catch(const std::invalid_argument& e)       { throw APIException(-57, "Invalid Parameter [", strKey, "]");           }
            catch(const std::out_of_range& e)           { throw APIException(-60, "[", strKey, "] out of range [", nLimit, "]"); }
        }

        throw APIException(-56, "Missing Parameter [", strKey, "]");
    }

}
