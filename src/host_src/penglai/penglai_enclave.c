/*
 * Copyright (c) IPADS@SJTU 2021. All rights reserved.
 * secGear is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "enclave.h"
#include "enclave_internal.h"
#include "enclave_log.h"
#include "penglai-enclave.h"
#include "penglai_enclave.h"
#define NONCE 12345
extern list_ops_management g_list_ops;

cc_enclave_result_t _penglai_create(cc_enclave_t *enclave, const enclave_features_t *features,
                                  const uint32_t features_count)
{
    if(enclave != NULL) {
        print_debug("enter function: _penglai_create\n");
    }
    cc_enclave_result_t result_cc;

    if (!enclave) {
        print_error_term("Context parameter error\n");
        return CC_ERROR_BAD_PARAMETERS;
    }

    /* penglai does not currently support feature */
    if (features != NULL || features_count > 0) {
        print_error_term("Penglai does not currently support additional features\n");
        return CC_ERROR_NOT_SUPPORTED;
    }

    struct elf_args* enclaveFile = malloc(sizeof(struct elf_args));
    elf_args_init(enclaveFile, enclave->path);
    if(!elf_valid(enclaveFile))
    {
        print_error_term("error when initializing enclaveFile\n");
        elf_args_destroy(enclaveFile);
        free(enclaveFile);
        return CC_ERROR_GENERIC;
    }

    struct PLenclave* penglai_enclave = malloc(sizeof(struct PLenclave));
    struct enclave_args* params = malloc(sizeof(struct enclave_args));
    PLenclave_init(penglai_enclave);
    enclave_param_init(params);
    params->untrusted_mem_size = DEFAULT_UNTRUSTED_SIZE;
    params->untrusted_mem_ptr = 0;

    if (PLenclave_create(penglai_enclave, enclaveFile, params)<0) {
        print_error_term("host: failed to create enclave\n");
        result_cc = CC_ERROR_GENERIC;

        /* clean states*/
        PLenclave_finalize(penglai_enclave);
        free(penglai_enclave);
        elf_args_destroy(enclaveFile);
        free(enclaveFile);
    } else {
        print_debug("penglai enclave create successfully! \n");
        enclave->private_data = (void *)penglai_enclave;
        result_cc = CC_SUCCESS;
    }
    PLenclave_attest(penglai_enclave,NONCE);
    enclave_param_destroy(params);
    free(params);
    return result_cc;
}

cc_enclave_result_t _penglai_destroy(cc_enclave_t *context)
{
    struct PLenclave* penglai_enclave;
    if (context != NULL) {
        print_debug("enter function: _penglai_destroy\n");
    }
    penglai_enclave = (struct PLenclave*) context->private_data;
    elf_args_destroy(penglai_enclave->elffile);
    free(penglai_enclave->elffile);
    PLenclave_finalize(penglai_enclave);
    free(penglai_enclave);
    return CC_SUCCESS;
}

cc_enclave_result_t handle_ocall(
    struct PLenclave* penglai_enclave,
    uint8_t* untrusted_mem_extent,
    const void *ocall_table,
    int* result)
{
    untrusted_mem_info_t* ocall_mem_info;
    size_t ocall_buf_size = 0;
    int ocall_table_size = 0;
    int ocall_func_id = 0;
    uint8_t* ocall_in_buf = NULL;
    uint8_t* ocall_out_buf = NULL;

    ocall_mem_info = (untrusted_mem_info_t*)untrusted_mem_extent;
    ocall_buf_size = size_to_aligned_size(sizeof(untrusted_mem_info_t)) +
            ocall_mem_info->in_buf_size + ocall_mem_info->out_buf_size;
    if(ocall_buf_size > DEFAULT_UNTRUSTED_SIZE){
        print_debug("[ERROR]: the size of ocall parameters is too \
            big to transfer through untrusted memory\n");
        return CC_FAIL;
    }

    ocall_table_size = ((ocall_enclave_table_t*)ocall_table)->num;
    ocall_func_id = ocall_mem_info->fid;
    if(ocall_func_id >= ocall_table_size){
        print_debug("[ERROR] host: ocall function isn't exist!\n");
        return CC_FAIL;
    }

    ocall_in_buf = untrusted_mem_extent +
            size_to_aligned_size(sizeof(untrusted_mem_info_t));
    ocall_out_buf = ocall_in_buf + ocall_mem_info->in_buf_size;
    if(((ocall_enclave_table_t*)ocall_table)->ocalls[ocall_func_id](
            ocall_in_buf,
            ocall_mem_info->in_buf_size,
            ocall_out_buf,
            ocall_mem_info->out_buf_size) != CC_SUCCESS){
        print_debug("[ERROR] host: ocall function return false!\n");
        return CC_FAIL;
    }
    penglai_enclave->user_param.ocall_buf_size = ocall_buf_size;
    penglai_enclave->user_param.resume_type = USER_PARAM_RESUME_FROM_CUSTOM_OCALL;
    *result = PLenclave_resume(penglai_enclave);

    return CC_SUCCESS;
}

// void printHexsecGearInhost(unsigned char *c, int n)
// {
//     int m = n / 16;
//     int left = n - m * 16;
//     char buf[33] = {0};
//     char num;
//     int top, below;
//     printf("n: %d, m: %d, left: %d\n", n, m, left);
//     for(int j = 0; j < m; j++){
//         for(int i = 0; i < 16; i++){
//             num = *(c + j*16 + i);
//             top = (num >> 4) & 0xF;
//             below = num & 0xF;
//             buf[2 * i] = (top < 10 ? '0'+top : 'a'+top-10);
//             buf[2 * i + 1] = (below < 10 ? '0'+below : 'a'+below-10);
//         }
//         buf[32] = '\0';
//         printf("%d - %d: %s\n", j*16, j*16+15, buf);
//     }
// 	if(left != 0){
//         for(int i = 0; i < left; i++){
//             num = *(c + m*16 + i);
//             top = (num >> 4) & 0xF;
//             below = num & 0xF;
//             buf[2 * i] = (top < 10 ? '0'+top : 'a'+top-10);
//             buf[2 * i + 1] = (below < 10 ? '0'+below : 'a'+below-10);
//         }
//         buf[2 * left] = '\0';
//         printf("%d - %d: %s\n", m*16, m*16+left-1, buf);
//     }
// }

cc_enclave_result_t cc_enclave_call_function(
    cc_enclave_t *enclave,
    uint32_t function_id,
    const void *input_buffer,
    size_t input_buffer_size,
    void *output_buffer,
    size_t output_buffer_size,
    void *ms,
    const void *ocall_table)
{
    untrusted_mem_info_t mem_info;
    size_t ecall_buf_size = 0;
    uint8_t* untrusted_mem_extent = NULL;
    uint8_t* in_buf = NULL;
    uint8_t* out_buf = NULL;
    struct PLenclave* penglai_enclave;
    int result = 0;
    cc_enclave_result_t result_cc;

    /* Penglai doesn't use message now */
    if(ms == NULL){
        print_debug("enter function: cc_enclave_call_function\n");
    }

    ecall_buf_size = size_to_aligned_size(sizeof(untrusted_mem_info_t)) +
        input_buffer_size + output_buffer_size;
    if(ecall_buf_size > DEFAULT_UNTRUSTED_SIZE){
        print_debug("[ERROR]: the size of parameters is \
            too big to transfer through untrusted memory\n");
        return CC_FAIL;
    }

    mem_info.fid = function_id;
    mem_info.in_buf_size = input_buffer_size;
    mem_info.out_buf_size = output_buffer_size;

    /* Allocate in_buf and out_buf contiguously.
    Untrusted_mem_extent always in user address space
    with respect to enclave's untrusted mem */
    untrusted_mem_extent = (uint8_t*)malloc(DEFAULT_UNTRUSTED_SIZE);
    if (untrusted_mem_extent == NULL) {
        return CC_ERROR_OUT_OF_MEMORY;
    }
    in_buf = untrusted_mem_extent +
            size_to_aligned_size(sizeof(untrusted_mem_info_t));
    out_buf = in_buf + input_buffer_size;
    memcpy(untrusted_mem_extent, &mem_info, sizeof(untrusted_mem_info_t));
    memcpy(in_buf, input_buffer, input_buffer_size);
    memcpy(out_buf, output_buffer, output_buffer_size);

    penglai_enclave = (struct PLenclave*)enclave->private_data;
    penglai_enclave->user_param.untrusted_mem_ptr =
            (unsigned long)untrusted_mem_extent;
    penglai_enclave->user_param.untrusted_mem_size = ecall_buf_size;
    result = PLenclave_run(penglai_enclave);
    while(result != 0){
        if(result == RETURN_USER_FOR_OCALL){
            result_cc = handle_ocall(penglai_enclave,
                untrusted_mem_extent, ocall_table, &result);
            if(result_cc != CC_SUCCESS){
                /* Return error values */
                free(untrusted_mem_extent);
                return result_cc;
            }
        } else {
            print_debug("[ERROR] PLenclave_run is failed with \
                    return value: %d \n", result);

            result_cc = CC_FAIL;
            /* Return error values */
            free(untrusted_mem_extent);
            return result_cc;
        }
    }

    memcpy(output_buffer, out_buf, output_buffer_size);
    // printf("[penglai_enclave.c host], input_buffer_size: %ld, output_buffer_size: %ld, meminfo_size: %ld\n", input_buffer_size, output_buffer_size, size_to_aligned_size(sizeof(untrusted_mem_info_t)));
    // printf("[penglai_enclave.c host], outbuffer:\n");
    // printHexsecGearInhost((unsigned char *)output_buffer, output_buffer_size);
    // printf("[penglai_enclave.c host], outbuffer end\n");
    result_cc = CC_SUCCESS;

    free(untrusted_mem_extent);
    return result_cc;
}

const struct cc_enclave_ops global_penglai_ops = {
    .cc_create_enclave = _penglai_create,
    .cc_destroy_enclave = _penglai_destroy,
    .cc_ecall_enclave = cc_enclave_call_function,
};

struct cc_enclave_ops_desc global_penglai_ops_name = {
    .name = "penglai",
    .ops = &global_penglai_ops,
    .type_version = PENGLAI_ENCLAVE_TYPE_0,
    .count = 0,
};

struct list_ops_desc global_penglai_ops_node = {
    .ops_desc = &global_penglai_ops_name,
    .next = NULL,
};

#define OPS_NAME global_penglai_ops_name
#define OPS_NODE global_penglai_ops_node
#define OPS_STRU global_penglai_ops

cc_enclave_result_t cc_tee_registered(cc_enclave_t *context, void *handle)
{
    size_t len = strlen(OPS_NAME.name);
    if (OPS_NAME.type_version != context->type || OPS_NODE.ops_desc != &OPS_NAME ||
        len >= MAX_ENGINE_NAME_LEN || OPS_NAME.ops != &OPS_STRU) {
        print_error_goto("The struct cc_enclave_ops_desc initialization error\n");
    }
    OPS_NAME.handle = handle;
    context->list_ops_node = &OPS_NODE;
    add_ops_list(&OPS_NODE);
    return  CC_SUCCESS;
done:
    return CC_ERROR_BAD_PARAMETERS;
}

cc_enclave_result_t cc_tee_unregistered(cc_enclave_t *context, enclave_type_version_t type_version)
{
    if (context == NULL || context->list_ops_node != &OPS_NODE || type_version != OPS_NAME.type_version) {
        print_error_goto("Engine parameter error \n");
    }
    remove_ops_list(&OPS_NODE);
    return  CC_SUCCESS;
done:
    return CC_FAIL;
}
