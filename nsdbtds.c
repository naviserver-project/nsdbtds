/* 
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1(the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis,WITHOUT WARRANTY OF ANY KIND,either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * Alternatively,the contents of this file may be used under the terms
 * of the GNU General Public License(the "GPL"),in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License,indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above,a recipient may use your
 * version of this file under either the License or the GPL.
 *
 * Author Vlad Seryakov vlad@crystalballinc.com
 * 
 * FreeTDS internal driver for NaviServer 4.x
 *
 * Requires FreeTDS version 0.64+
 *
 * Derived from FreeTDS driver by Dossy <dossy@panoptic.com>
 */

#include "ns.h"
#include "nsdb.h"
#include "tds.h"
#include "tdsconvert.h"

typedef struct {
   TDSSOCKET *tds;
   TDSLOGIN *login;
   TDSCONTEXT *context;
} Db_Handle;

/* Common system headers */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#define MAX_IDENTIFIER 256
#define GET_TDS(handle) ((Db_Handle*)handle->connection)->tds
#define GET_TDS_LOGIN(handle) ((Db_Handle*)handle->connection)->login
#define GET_TDS_CONTEXT(handle) ((Db_Handle*)handle->connection)->context
#define GET_TDS_RESULTS(handle) ((TDSRESULTINFO *)handle->statement)

static char *freetds_driver_name = "nsfreetds";
static char *freetds_driver_version = "FreeTDS Driver v0.5";

static char *Db_Name(void);
static char *Db_DbType(Ns_DbHandle *handle);
static int Db_ServerInit(char *hServer,char *hModule,char *hDriver);
static int Db_OpenDb(Ns_DbHandle *handle);
static int Db_CloseDb(Ns_DbHandle *handle);
static int Db_GetRow(Ns_DbHandle *handle,Ns_Set *row);
static int Db_Flush(Ns_DbHandle *handle);
static int Db_Cancel(Ns_DbHandle *handle);
static int Db_Exec(Ns_DbHandle *handle,char *sql);
static Ns_Set *Db_BindRow(Ns_DbHandle *handle);
static int Db_SpStart(Ns_DbHandle *handle,char *procname);
static int Db_SpExec(Ns_DbHandle *handle);
static int Db_Msg_Handler(const TDSCONTEXT *,TDSSOCKET *,TDSMESSAGE *);
static int Db_Err_Handler(const TDSCONTEXT *,TDSSOCKET *tds,TDSMESSAGE *);

static Ns_DbProc freetdsProcs[] = {
    { DbFn_Name,         Db_Name },
    { DbFn_DbType,       Db_DbType },
    { DbFn_ServerInit,   (void *) Db_ServerInit },
    { DbFn_OpenDb,       Db_OpenDb },
    { DbFn_CloseDb,      Db_CloseDb },
    { DbFn_GetRow,       Db_GetRow },
    { DbFn_Flush,        Db_Flush },
    { DbFn_Cancel,       Db_Cancel },
    { DbFn_Exec,         (void *) Db_Exec },
    { DbFn_BindRow,      (void *) Db_BindRow },
    { DbFn_SpStart,      Db_SpStart },
    { DbFn_SpExec,       Db_SpExec },
    { 0, NULL }
};


DllExport int Ns_ModuleVersion = 1;
DllExport int Ns_ModuleFlags = 0;

DllExport int
Ns_DbDriverInit(char *hDriver, char *configPath)
{
    if(hDriver == NULL) {
      Ns_Log(Bug, "Db_DriverInit():  NULL driver name.");
      return NS_ERROR;
    }

    if(Ns_DbRegisterDriver(hDriver, &(freetdsProcs[0])) != NS_OK) {
      Ns_Log(Error,"Db_DriverInit(%s):  Could not register the %s driver.",
             hDriver, freetds_driver_name);
      return NS_ERROR;
    }

    Ns_Log(Notice, "Db_DriverInit(%s):  Loaded %s, %s, built on %s at %s.",
           hDriver, freetds_driver_version, TDS_VERSION_NO, __DATE__, __TIME__);

    return NS_OK;
}

static char *
Db_Name(void)
{
    return freetds_driver_name;
}


static char *
Db_DbType(Ns_DbHandle *handle)
{
    return TDS_VERSION_NO;
}

static int
Db_OpenDb(Ns_DbHandle *handle)
{
    TDSSOCKET *tds;
    TDSLOGIN *login;
    TDSCONTEXT *context;
    TDSCONNECTION *connect;
    Db_Handle *nsdb;

    login = tds_alloc_login();
    context = tds_alloc_context(0);
    tds = tds_alloc_socket(context,512);

    if(context->locale && !context->locale->date_fmt) {
      context->locale->date_fmt = strdup("%Y-%m-%d %T");
    }

    context->msg_handler = Db_Msg_Handler;
    context->err_handler = Db_Err_Handler;

    tds_set_app(login,Db_Name());
    tds_set_server(login,handle->datasource);
    tds_set_user(login,handle->user);
    tds_set_passwd(login,handle->password);
    tds_set_host(login,Ns_InfoServerName());
    tds_set_library(login,"TDS-Library");
    tds_set_language(login,"us_english");
    tds_set_packet(login,2048);
    tds_set_parent(tds,handle);

    connect = tds_read_config_info(NULL,login,context->locale);

    if(!connect || tds_connect(tds,connect) == TDS_FAIL) {;
      Ns_Log(Notice, "Db_OpenDb(%s): tds_connect() failed.",handle->datasource);
      if(connect) tds_free_connection(connect);
      tds_free_socket(tds);
      tds_free_login(login);
      tds_free_context(context);
      return NS_ERROR;
    }
    tds_free_connection(connect);
    nsdb = (Db_Handle*)ns_malloc(sizeof(Db_Handle));
    nsdb->tds = tds;
    nsdb->login = login;
    nsdb->context = context;
    handle->connection = nsdb;
    handle->connected = NS_TRUE;

    return NS_OK;
}

static int
Db_CloseDb(Ns_DbHandle *handle)
{
    Db_Cancel(handle);

    tds_free_login(GET_TDS_LOGIN(handle));
    tds_free_context(GET_TDS_CONTEXT(handle));
    tds_free_socket(GET_TDS(handle));
    ns_free(handle->connection);
    handle->connection = 0;
    handle->connected = NS_FALSE;

    return NS_OK;
}

static int
Db_Exec(Ns_DbHandle *handle, char *sql)
{
    int status = TDS_SUCCEED, rc = NS_DML, done = 0;
    TDS_INT resulttype;

    if(Db_Cancel(handle) == NS_ERROR) {
       return NS_ERROR;
    }

    if(tds_submit_query(GET_TDS(handle),sql) != TDS_SUCCEED) {
       Ns_Log(Error, "Db_Exec(%s): tds_submit_query failed.", handle->datasource);
       return NS_ERROR;
    }
    while(status == TDS_SUCCEED) {
      status = tds_process_tokens(GET_TDS(handle),&resulttype,&done,TDS_TOKEN_RESULTS);
      switch(resulttype) {
       case TDS_DONE_RESULT:
       case TDS_DONEPROC_RESULT:
       case TDS_DONEINPROC_RESULT:
           if(done & TDS_DONE_ERROR) status = TDS_FAIL;
           break;
       case TDS_ROW_RESULT:
       case TDS_ROWFMT_RESULT: 
       case TDS_COMPUTE_RESULT:
           handle->statement = (void *)GET_TDS(handle)->res_info;
           if(rc != NS_ERROR && handle->statement) {
              handle->fetchingRows = 1;
              rc = NS_ROWS;
              status = TDS_NO_MORE_RESULTS;
           }
           break;
      }
      
    }
    if((status != TDS_SUCCEED && status != TDS_NO_MORE_RESULTS) || handle->dsExceptionMsg.length) {
       handle->statement = NULL;
       handle->fetchingRows = 1;
       return NS_ERROR;
    }
    return rc;
}

static int
Db_GetRow(Ns_DbHandle *handle, Ns_Set *row)
{
    int i,rc,ctype;
    TDSCOLUMN *col;
    CONV_RESULT dres;
    unsigned char *src;
    TDS_INT srclen,resulttype,computeid;

    if(!handle->fetchingRows || !handle->row->size) {
       Ns_DbSetException(handle,"NSDB","no rows waiting to fetch");
       return NS_ERROR;
    }
    rc = tds_process_tokens(GET_TDS(handle),&resulttype,&computeid,TDS_STOPAT_ROWFMT|TDS_RETURN_DONE|TDS_RETURN_ROW|TDS_RETURN_COMPUTE);
    if(rc != TDS_SUCCEED && rc != TDS_NO_MORE_RESULTS) {
       Ns_Log(Error,"Db_GetRow(%s): tds_process_row_tokens: %d",handle->datasource,rc);
       Db_Cancel(handle);
       return NS_ERROR;
    }
    if(rc == TDS_NO_MORE_RESULTS || 
       !GET_TDS(handle)->res_info ||
       !GET_TDS(handle)->current_results ||
       (resulttype != TDS_ROW_RESULT && resulttype != TDS_COMPUTE_RESULT)) {
      handle->statement = NULL;
      handle->fetchingRows = 0;
      return NS_END_DATA;
    }
    for(i = 0; i < GET_TDS(handle)->res_info->num_cols; i++) {
      col = GET_TDS(handle)->res_info->columns[i];
      if(col->column_cur_size < 0) {
        Ns_SetPutValue(row,i,"");
        continue;
      }
      ctype = tds_get_conversion_type(col->column_type,col->column_size);
      src = &(GET_TDS(handle)->res_info->current_row[col->column_offset]);
      if(is_blob_type(col->column_type)) src = (unsigned char *)((TDSBLOB *)src)->textvalue;
      srclen = col->column_cur_size;
      if(tds_convert(GET_TDS(handle)->tds_ctx,ctype,(TDS_CHAR *)src,srclen,SYBVARCHAR,&dres) < 0) {
        Ns_SetPutValue(row,i,"");
        continue;
      }
      Ns_SetPutValue(row,i,dres.c);
      free(dres.c);
    }
    return NS_OK;
}

static int
Db_Flush(Ns_DbHandle *handle)
{
    return Db_Cancel(handle);
}

static int
Db_Cancel(Ns_DbHandle *handle)
{
    if(IS_TDSDEAD(GET_TDS(handle))) {
       Ns_Log(Error, "Db_Cancel(%s): dead connection detected.", handle->datasource);
       tds_free_all_results(GET_TDS(handle));
       handle->statement = NULL;
       handle->fetchingRows = 0;
       handle->connected = NS_FALSE;
       return NS_ERROR;
    }
    if(tds_process_simple_query(GET_TDS(handle)) == TDS_FAIL) {
       Ns_Log(Error,"Db_Cancel(%s): tds_process_simple_query failed",handle->datasource);
    }
    tds_free_all_results(GET_TDS(handle));
    tds_send_cancel(GET_TDS(handle));
    tds_process_cancel(GET_TDS(handle));
    handle->statement = NULL;
    handle->fetchingRows = 0;
    return NS_OK;
}

static Ns_Set *
Db_BindRow(Ns_DbHandle *handle)
{
    int i;

    if(GET_TDS_RESULTS(handle)) {
      for(i = 0; i < GET_TDS_RESULTS(handle)->num_cols; i++) {
        Ns_SetPut((Ns_Set *)handle->row,GET_TDS_RESULTS(handle)->columns[i]->column_name,NULL);
      }
    }
    return (Ns_Set *)handle->row;
}

static int
Db_SpStart(Ns_DbHandle *handle, char *procname)
{
    if(Db_Exec(handle, procname) != NS_ERROR) {
       return NS_OK;
    }
    return NS_ERROR;
}

static int
Db_SpExec(Ns_DbHandle *handle)
{
    if(GET_TDS(handle)->res_info == NULL) {
       return NS_DML;
    }
    return NS_ROWS;
}


int Db_Err_Handler(const TDSCONTEXT *ctx,TDSSOCKET *tds,TDSMESSAGE *msg)
{
    Ns_DbHandle *handle = (Ns_DbHandle *)tds->parent;

    Ns_Log(Error,"Db_Err_Handler(%s): ERR(%u:%u) %s",
           handle->datasource,msg->msg_number,msg->line_number,msg->message);
    Ns_DbSetException(handle, "NSDB", msg->message);
    return 0;
}

int Db_Msg_Handler(const TDSCONTEXT *ctx,TDSSOCKET *tds,TDSMESSAGE *msg)
{
    Ns_DbHandle *handle = (Ns_DbHandle *)tds->parent;

    if(handle->verbose) {
       Ns_Log(Notice, "Db_Msg_Handler(%s:%d,%d,%s): %s",
              handle->datasource,msg->msg_level,msg->msg_state,
              msg->sql_state ? msg->sql_state : "0",msg->message);
    }
    if(msg->msg_level > 10) {
       Ns_DbSetException(handle, "NSDB", msg->message);
    }
    return 0;
}


static void
Db_Rows_Affected(Tcl_Interp *interp, Ns_DbHandle *handle)
{
    Tcl_SetObjResult(interp, Tcl_NewLongObj(GET_TDS(handle)->rows_affected));
}


/*
 * Db_Cmd - This function implements the "ns_freetds" Tcl command
 * installed into each interpreter of each virtual server.  It provides
 * access to features specific to the FreeTDS driver.
 */

static int
Db_Cmd(ClientData arg,Tcl_Interp *interp,int objc,Tcl_Obj * CONST objv[])
{
    int cmd;
    Ns_DbHandle *handle;
    enum commands {
        cmdRowsAffected, cmdVersion
    };

    static const char *sCmd[] = {
        "rows_affected", "version",
        0
    };

    if(objc < 3) {
      Tcl_AppendResult(interp,"wrong # args: should be: ",Tcl_GetString(objv[0])," cmd handle ?args?",0);
      return TCL_ERROR;
    }
    if(Tcl_GetIndexFromObj(interp,objv[1],sCmd,"command",TCL_EXACT,(int *)&cmd) != TCL_OK)
      return TCL_ERROR;

    if(Ns_TclDbGetHandle(interp,Tcl_GetString(objv[2]),&handle) != TCL_OK) return TCL_ERROR;

    /* Make sure this is a FreeTDS handle before accessing handle->connection. */
    if(Ns_DbDriverName(handle) != freetds_driver_name) {
      Tcl_AppendResult(interp,"handle \"",Tcl_GetString(objv[2]),"\" is not of type \"",freetds_driver_name,"\"",NULL);
      return TCL_ERROR;
    }

    switch(cmd) {
     case cmdRowsAffected:
        Db_Rows_Affected(interp,handle);
        break;
     case cmdVersion:
        Tcl_SetResult(interp,freetds_driver_version,TCL_STATIC);
        break;
    }
    return TCL_OK;
}

static int
Db_InterpInit(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateObjCommand(interp, "ns_freetds", Db_Cmd, NULL, NULL);
    return NS_OK;
}


static int
Db_ServerInit(char *hServer, char *hModule, char *hDriver)
{
    return Ns_TclInitInterps(hServer, Db_InterpInit, NULL);
}


