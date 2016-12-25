/*
Copyright (c) 2013, David C Horton

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <algorithm>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>

namespace drachtio {
    class SipDialogController ;
}

#define NTA_RELIABLE_MAGIC_T drachtio::SipDialogController

#include "controller.hpp"
#include "cdr.hpp"
#include "sip-dialog-controller.hpp"

namespace {
    void cloneRespondToSipRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doRespondToSipRequest( d ) ;
    }
    void cloneSendSipRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doSendRequestOutsideDialog( d ) ;
    }
    void cloneSendSipCancelRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doSendCancelRequest( d ) ;
    }
    int uacLegCallback( nta_leg_magic_t* p, nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processRequestInsideDialog( leg, irq, sip) ;
    }
    int uasCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
       drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processCancelOrAck( p, irq, sip) ;
    }
    int uasPrack( drachtio::SipDialogController *pController, nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        return pController->processPrack( rel, prack, sip) ;
    }
   int response_to_request_outside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseOutsideDialog( request, sip ) ;
    } 
   int response_to_request_inside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseInsideDialog( request, sip ) ;
    } 
    void cloneSendSipRequestInsideDialog(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doSendRequestInsideDialog( d ) ;
    }
    int response_to_refreshing_reinvite( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseToRefreshingReinvite( request, sip ) ;
    }
}


namespace drachtio {

	SipDialogController::SipDialogController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent()), m_pClientController(pController->getClientController()) {

            assert(m_agent) ;
            assert(m_pClientController) ;


	}
	SipDialogController::~SipDialogController() {
	}
    bool SipDialogController::sendRequestInsideDialog( const string& clientMsgId, const string& dialogId, const string& startLine, const string& headers, const string& body, string& transactionId ) {

        assert( dialogId.length() > 0 ) ;

        if( 0 == transactionId.length() ) { generateUuid( transactionId ) ; }

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequestInsideDialog, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error allocating message") ;
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", dialogId, startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error sending message") ;
            return  false;
        }
        
        return true ;
    }
    ///client-initiated outgoing messages (stack thread)
    void SipDialogController::doSendRequestInsideDialog( SipMessageData* pData ) {                
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string myHostport ;
        string requestUri ;
        string name ;

        DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog dialog id: " << pData->getDialogId()  ;

        sip_method_t method = parseStartLine( pData->getStartLine(), name, requestUri ) ;

        boost::shared_ptr<SipDialog> dlg ;
 
        assert( pData->getDialogId() ) ;

        try {

            if( !findDialogById( pData->getDialogId(), dlg ) ) {
                if( sip_method_ack == method ) {
                    DR_LOG(log_debug) << "Can't send ACK for dialog id " << pData->getDialogId() 
                        << "; likely because stack already ACK'ed non-success final response" ;
                    throw std::runtime_error("ACK for non-success final response is automatically generated by server") ;
                }
                DR_LOG(log_debug) << "Can't find dialog for dialog id " << pData->getDialogId() ;
                //assert(false) ;
                throw std::runtime_error("unable to find dialog for dialog id provided") ;
            }

            string sourceAddress = dlg->getSourceAddress() ;
            unsigned int sourcePort = dlg->getSourcePort() ;
            string routeUrl = string("sip:") + sourceAddress + ":" + boost::lexical_cast<std::string>(sourcePort) + 
                ";transport=" + dlg->getProtocol() ;
            DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - sending request to " << routeUrl ;

            string transport ;
            dlg->getTransportDesc(transport) ;
            tagi_t* tags = makeTags( pData->getHeaders(), transport) ;

            tport_t* tp = dlg->getTport() ; 
            bool forceTport = NULL != tp ;  


            nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
            if( !leg ) {
                assert( leg ) ;
                throw std::runtime_error("unable to find active leg for dialog") ;
            }

            const sip_contact_t *target ;
            if( string::npos != requestUri.find("placeholder") && nta_leg_get_route( leg, NULL, &target ) >=0 ) {
                char buffer[256];
                url_e( buffer, 255, target->m_url ) ;
                requestUri = buffer ;
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - defaulting request uri to " << requestUri  ;
            }

            if( method == sip_method_invalid || method == sip_method_unknown ) {
                throw std::runtime_error(string("invalid or missing method supplied on start line: ") + pData->getStartLine() ) ;
            }

            //set content-type if not supplied and body contains SDP
            string body = pData->getBody() ;
            string contentType ;
            if( body.length() && !searchForHeader( tags, siptag_content_type, contentType ) ) {
                if( 0 == body.find("v=0") ) {
                    contentType = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - automatically detecting content-type as application/sdp"  ;
                }
                else {
                    throw std::runtime_error("missing content-type") ;                   
                }
            }
            if( sip_method_invite == method && body.length() && 0 == contentType.compare("application/sdp")) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - updating local sdp to " << body ;
                dlg->setLocalSdp( body.c_str() ) ;
            }

            if( sip_method_ack == method ) {
                if( 200 == dlg->getSipStatus() ) {
                    orq = nta_outgoing_tcreate(leg, NULL, NULL, NULL, SIP_METHOD_ACK,
                        URL_STRING_MAKE(requestUri.c_str()) ,
                        TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str())),
                        TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str())),
                        TAG_IF(forceTport, NTATAG_TPORT(tp)),
                        TAG_NEXT(tags) ) ;
                    dlg->ackSent() ;  
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - clearing IIP that we generated as uac" ;
                    this->clearIIP( leg ) ;                 
                }
            }
            else if( sip_method_prack == method ) {
                shared_ptr<IIP> iip ;
                if( !findIIPByLeg( leg, iip ) ) {
                    throw std::runtime_error("unable to find IIP when sending PRACK") ;
                }
                orq = nta_outgoing_prack(leg, iip->orq(), response_to_request_inside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    //NULL, 
                    URL_STRING_MAKE( routeUrl.c_str() ),
                    NULL, TAG_NEXT(tags) ) ;
            }
            else {
                string contact ;
                bool addContact = false ;
                if( (method == sip_method_invite || method == sip_method_subscribe) && !searchForHeader( tags, siptag_contact_str, contact ) ) {
                    //TODO: should get this from dlg->m_tp I think....half the time the below is incorrect in that it refers to the remote end
                    contact = "<" ;
                    contact.append( ( 0 == dlg->getProtocol().compare("tls") ? "sips:" : "sip:") ) ;
                    contact.append( dlg->getTransportAddress() ) ;
                    contact.append( ":" ) ;
                    contact.append( dlg->getTransportPort() ) ;
                    contact.append(">") ;
                    addContact = true ;

                }
                orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    URL_STRING_MAKE( routeUrl.c_str() ),                     
                    method, name.c_str()
                    ,URL_STRING_MAKE(requestUri.c_str())
                    ,TAG_IF( addContact, SIPTAG_CONTACT_STR( contact.c_str() ) )
                    ,TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str()))
                    ,TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                    ,TAG_IF(forceTport, NTATAG_TPORT(tp))
                    ,TAG_NEXT(tags) ) ;
            }

            deleteTags( tags ) ;

            if( NULL == orq && sip_method_ack != method ) {
                throw std::runtime_error("Error creating sip transaction for request") ;               
            }

            if( sip_method_ack == method && 200 != dlg->getSipStatus() ) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - clearing uac dialog that had final response " <<  dlg->getSipStatus() ;
                clearDialog(dlg->getDialogId()) ;
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    "ACK for non-success responses is automatically generated by the stack" ) ;
            }
            else {
                msg_t* m = nta_outgoing_getrequest(orq) ;  // adds a reference
                sip_t* sip = sip_object( m ) ;

                if( sip_method_ack == method ) {
                    if( dlg->getSipStatus() > 200 ) {
                        m_pClientController->removeDialog( dlg->getDialogId() ) ;
                    }
                    nta_outgoing_destroy( orq ) ;
                }
                else {
                    boost::shared_ptr<RIP> p = boost::make_shared<RIP>( pData->getTransactionId(), pData->getDialogId() ) ;
                    addRIP( orq, p ) ;       
                }
                if( sip_method_invite == method ) {
                    addOutgoingInviteTransaction( leg, orq, sip, dlg ) ;
                }
     
                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(m, orq) ;
                string s ;
                meta.toMessageFormat(s) ;
                string data = s + "|" + pData->getTransactionId() + "|Msg sent:|" + DR_CRLF + encodedMessage ;

                msg_destroy(m) ; //releases reference
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ;                
            }

 
        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendRequestInsideDialog - Error: " << err.what() ;
            string msg = string("Server error: ") + err.what() ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", msg ) ;
            m_pController->getClientController()->removeAppTransaction( pData->getTransactionId() ) ;
        }                       

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
    }

//send request outside dialog
    //client thread
    bool SipDialogController::sendRequestOutsideDialog( const string& clientMsgId, const string& startLine, const string& headers, const string& body, string& transactionId, string& dialogId ) {
        if( 0 == transactionId.length() ) { generateUuid( transactionId ) ; }
        if( string::npos != startLine.find("INVITE") ) {
            generateUuid( dialogId ) ;
        }

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", dialogId, startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    //stack thread
     void SipDialogController::doSendRequestOutsideDialog( SipMessageData* pData ) {
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string requestUri ;
        string name ;
        string sipOutboundProxy ;
        tport_t* tp = NULL ;

        try {
            bool useOutboundProxy = m_pController->getConfig()->getSipOutboundProxy( sipOutboundProxy ) ;
            bool forceTport = false ;

            sip_request_t *sip_request = sip_request_make(m_pController->getHome(), pData->getStartLine() ) ;
            if( NULL == sip_request || 
                url_invalid == sip_request->rq_url[0].url_type || 
                url_unknown == sip_request->rq_url[0].url_type  ||
                sip_method_invalid == sip_request->rq_method ||
                sip_method_unknown == sip_request->rq_method  ) {

                throw std::runtime_error(string("invalid request-uri: ") + pData->getStartLine() ) ;
            }
            sip_method_t method = parseStartLine( pData->getStartLine(), name, requestUri ) ;

            int rc = 0 ;
            if( NULL != strstr( sip_request->rq_url->url_host, ".invalid") ) {
                boost::shared_ptr<SipProxyController::UaInvalidData> pData = 
                    m_pController->getProxyController()->findTportForSubscription( sip_request->rq_url->url_user, sip_request->rq_url->url_host ) ;

                if( NULL != pData ) {
                    tp = pData->getTport() ;
                    forceTport = true ;
                    DR_LOG(log_debug) << "SipProxyController::doSendRequestOutsideDialog forcing tport to reach .invalid domain " << std::hex << (void *) tp ;
               }
            }
            if( NULL == tp ) {
                rc = nta_get_outbound_tport_name_for_url( m_pController->getAgent(), m_pController->getHome(), 
                            URL_STRING_MAKE(requestUri.c_str()), (void **) &tp ) ;
                assert( 0 == rc ) ;                
            }
            su_free( m_pController->getHome(), sip_request ) ;

            string desc ;
            getTransportDescription( tp, desc ) ;
            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - selected transport " << desc << " for request-uri " << requestUri  ;            

            tagi_t* tags = makeTags( pData->getHeaders(), desc ) ;
           
            //if user supplied all or part of the From use it
            const tp_name_t* tpn = tport_name( tport_parent( tp ) );
            string host = tpn->tpn_host ;
            string port = tpn->tpn_port ;
            string proto = tpn->tpn_proto ;

            string contact = "<sip:" + host + ":" + port + ">;transport=" + proto;
            string from, to, callid ;
            if( searchForHeader( tags, siptag_from_str, from ) ) {
                if( !replaceHostInUri( from, host.c_str(), port.c_str() ) ) {
                    throw std::runtime_error(string("invalid from value provided by client: ") + from ) ;
                }
            } 
            else {
                from = contact ;
            }

            //default To header to request uri if not provided
            if( !searchForHeader( tags, siptag_to_str, to ) ) {
                to = requestUri ;
            } 

            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - from: " << from   ;            
            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - to: " << to ;            
            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - contact: " << contact  ;            

            // use call-id if supplied
            if( searchForHeader( tags, siptag_call_id_str, callid ) ) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - using client-specified call-id: " << callid  ;            
            }

            //set content-type if not supplied and body contains SDP
            string body = pData->getBody() ;
            string contentType ;
            if( body.length() && !searchForHeader( tags, siptag_content_type, contentType ) ) {
                if( 0 == body.find("v=0") ) {
                    contentType = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - automatically detecting content-type as application/sdp"  ;
                }
                else {
                    throw std::runtime_error("missing content-type") ;                   
                }
             }

            //prevent looping messages
            normalizeSipUri( requestUri, 0 ) ;
            if( isLocalSipUri( requestUri ) ) {
                throw std::runtime_error("can not send request to myself") ;
            }

            if( !(leg = nta_leg_tcreate( m_pController->getAgent(),
                uacLegCallback, (nta_leg_magic_t *) m_pController,
                SIPTAG_FROM_STR(from.c_str()),
                SIPTAG_TO_STR(to.c_str()),
                TAG_IF( callid.length(), SIPTAG_CALL_ID_STR(callid.c_str())),
                TAG_IF( method == sip_method_register, NTATAG_NO_DIALOG(1)),
                TAG_END() ) ) ) {

                throw std::runtime_error("Error creating leg") ;
            }
            nta_leg_tag( leg, NULL ) ;

            orq = nta_outgoing_tcreate( leg, 
                response_to_request_outside_dialog, 
                (nta_outgoing_magic_t*) m_pController, 
                useOutboundProxy ? URL_STRING_MAKE( sipOutboundProxy.c_str() ) : NULL, 
                method, 
                name.c_str()
                ,URL_STRING_MAKE(requestUri.c_str())
                ,TAG_IF( (method == sip_method_invite || method == sip_method_subscribe) && 
                    !searchForHeader( tags, siptag_contact_str, contact ), SIPTAG_CONTACT_STR( contact.c_str() ) )
                ,TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str()))
                ,TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                ,TAG_IF(forceTport, NTATAG_TPORT(tp))
                ,TAG_NEXT(tags) ) ;

            deleteTags( tags ) ;

            if( NULL == orq ) {
                throw std::runtime_error("Error creating sip transaction for uac request") ;               
            }

            msg_t* m = nta_outgoing_getrequest(orq) ; //adds a reference
            sip_t* sip = sip_object( m ) ;

            if( method == sip_method_invite ) {
                boost::shared_ptr<SipDialog> dlg = boost::make_shared<SipDialog>( pData->getDialogId(), pData->getTransactionId(), 
                    leg, orq, sip, m, desc ) ;
                addOutgoingInviteTransaction( leg, orq, sip, dlg ) ;          
            }
            else {
                boost::shared_ptr<RIP> p = boost::make_shared<RIP>( pData->getTransactionId(), pData->getDialogId() ) ;
                addRIP( orq, p ) ;
                nta_leg_destroy( leg ) ;
            }

            string encodedMessage ;
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta(m, orq) ;
            string s ;
            meta.toMessageFormat(s) ;
            msg_destroy(m) ;    // releases reference

            string data = s + "|" + pData->getTransactionId() + "|Msg sent:|" + DR_CRLF + encodedMessage ;

            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ;
           
 
        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendRequestOutsideDialog - " << err.what() ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", err.what() ) ;  
            m_pController->getClientController()->removeAppTransaction( pData->getTransactionId() ) ;
        }                       

        //N.B.: we must explicitly call the destructor of an object allocated with placement new
        pData->~SipMessageData() ; 
    }

    bool SipDialogController::sendCancelRequest( const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) {
        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipCancelRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", "", startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    bool SipDialogController::respondToSipRequest( const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) {
       su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneRespondToSipRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false ;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string rid ;
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", "", startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false ;
        }

        return true ;
    }


   
    void SipDialogController::doSendCancelRequest( SipMessageData* pData ) {
        string transactionId( pData->getTransactionId() ) ;
        boost::shared_ptr<IIP> iip ;

        if( findIIPByTransactionId( transactionId, iip ) ) {
            nta_outgoing_t *cancel = nta_outgoing_tcancel(iip->orq(), NULL, NULL, TAG_NULL());
            if( NULL != cancel ) {
                msg_t* m = nta_outgoing_getrequest(cancel) ;    // adds a reference
                sip_t* sip = sip_object( m ) ;

                string cancelTransactionId ;
                generateUuid( cancelTransactionId ) ;

                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(m, cancel) ;
                string s ;
                meta.toMessageFormat(s) ;
                string data = s + "|" + cancelTransactionId + "|Msg sent:|" + DR_CRLF + encodedMessage ;
                msg_destroy(m) ;

                //Note: not adding an RIP because the 200 OK to the CANCEL is not passed up to us
                //boost::shared_ptr<RIP> p = boost::make_shared<RIP>( cancelTransactionId, iip->dlg() ? iip->dlg()->getDialogId() : "" ) ;
                //addRIP( cancel, p ) ;       


                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ;     
                return ;           
            }
            else {
                DR_LOG(log_error) << "SipDialogController::doSendCancelRequest - internal server error canceling transaction id " << transactionId ;
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", 
                    string("internal server error canceling transaction id: ") + transactionId ) ; 
            }
        }
        else {
            DR_LOG(log_error) << "SipDialogController::doSendCancelRequest - unknown transaction id " << transactionId ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", 
                string("unable to cancel unknown transaction id: ") + transactionId ) ; 
        }

        pData->~SipMessageData() ;
   }

    int SipDialogController::processResponseOutsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        DR_LOG(log_debug) << "SipDialogController::processResponseOutsideDialog"  ;
        string transactionId ;
        boost::shared_ptr<SipDialog> dlg ;

        if( sip->sip_cseq->cs_method == sip_method_invite ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByOrq( orq, iip ) ) {
                DR_LOG(log_error) << "SipDialogController::processResponse - unable to match invite response with callid: " << sip->sip_call_id->i_id  ;
                //TODO: do I need to destroy this transaction?
                return -1 ; //TODO: check meaning of return value           
            }      
            transactionId = iip->getTransactionId() ;   
            dlg = iip->dlg() ;   

            //check for retransmission 
            if( dlg->getSipStatus() >= 200 && dlg->getSipStatus() == sip->sip_status->st_status ) {
                if( dlg->hasAckBeenSent() ) {
                    DR_LOG(log_warning) << "SipDialogController::processResponse - received retransmitted final response: " << sip->sip_status->st_status 
                        << " " << sip->sip_status->st_phrase << ": note we currently are not retransmitting the ACK (bad)" ;
                }
                else {
                    DR_LOG(log_warning) << "SipDialogController::processResponse - received retransmitted final response: " << sip->sip_status->st_status 
                        << " " << sip->sip_status->st_phrase << ": ACK has not yet been sent by app" ;                    
                }
                return 0 ;
            }
            //update dialog variables
            
            dlg->setSipStatus( sip->sip_status->st_status ) ;
            if( sip->sip_payload ) {
                iip->dlg()->setRemoteSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
            }

            //UAC Dialog is added when we receive a final response from the network, or a reliable provisional response
            //for non-success responses, it will subsequently be removed when we receive the ACK from the client

            if( 200 == sip->sip_status->st_status ||
                (sip->sip_status->st_status > 100 && sip->sip_status->st_status < 200 && sip->sip_rseq) ) {
                DR_LOG(log_error) << "SipDialogController::processResponse - adding dialog id: " << dlg->getDialogId()  ;
                nta_leg_t* leg = iip->leg() ;
                nta_leg_rtag( leg, sip->sip_to->a_tag) ;
                nta_leg_client_reroute( leg, sip->sip_record_route, sip->sip_contact, false );
                addDialog( dlg ) ;
            }
            if( sip->sip_status->st_status > 200 ){
                dlg->ackSent() ;
                clearIIP( iip->leg() ) ;
            }
        }
        else {
            boost::shared_ptr<RIP> rip ;
            if( !findRIPByOrq( orq, rip ) ) {
                DR_LOG(log_error) << "SipDialogController::processResponse - unable to match response with callid for a non-invite request we sent: " << sip->sip_call_id->i_id  ;
                //TODO: do I need to destroy this transaction?
                return -1 ; //TODO: check meaning of return value                           
            }
            transactionId = rip->getTransactionId() ;
            if( sip->sip_status->st_status >= 200 ) this->clearRIP( orq ) ;
        }
        
        string encodedMessage ;
        bool truncated ;
        msg_t* msg = nta_outgoing_getresponse(orq) ;    //adds a reference
        SipMsgData_t meta( msg, orq, "network") ;
        msg_destroy( msg ) ;                            //releases reference

        EncodeStackMessage( sip, encodedMessage ) ;

        if( !dlg ) {
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, transactionId ) ;
        }
        else {
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, transactionId, dlg->getDialogId() ) ;            
        }

        if( sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status > 200 ) {
            assert( dlg ) ;
            m_pController->getClientController()->removeDialog( dlg->getDialogId() ) ;
        }

        return 0 ;
    }
    void SipDialogController::doRespondToSipRequest( SipMessageData* pData ) {
        string transactionId( pData->getTransactionId() );
        string startLine( pData->getStartLine()) ;
        string headers( pData->getHeaders() );
        string body( pData->getBody()) ;
        string clientMsgId( pData->getClientMsgId()) ;
        string contentType ;
        string dialogId ;
        bool bSentOK = true ;
        string failMsg ;
        bool bDestroyIrq = false ;

        //decode status 
        sip_status_t* sip_status = sip_status_make( m_pController->getHome(), startLine.c_str() ) ;
        int code = sip_status->st_status ;
        const char* status = sip_status->st_phrase ;
  
        nta_incoming_t* irq = NULL ;
        int rc = -1 ;
        boost::shared_ptr<IIP> iip ;

        /* search for requests within a dialog first */
        irq = findAndRemoveTransactionIdForIncomingRequest( transactionId ) ;
        if( !irq ) {
             if( !findIIPByTransactionId( transactionId, iip ) ) {
                /* could be a new incoming request that hasn't been responded to yet */
                if( m_pController->setupLegForIncomingRequest( transactionId ) ) {
                    if( !findIIPByTransactionId( transactionId, iip ) ) {
                        irq = findAndRemoveTransactionIdForIncomingRequest( transactionId )  ;
                    }
                }
             }
        }

        if( irq ) {

            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest found incoming transaction " << std::hex << irq  ;

            msg_t* msg = nta_incoming_getrequest( irq ) ;
            sip_t *sip = sip_object( msg );

            tport_t *tp = nta_incoming_transport(m_agent, irq, msg) ; 
            tport_t *tport = tport_parent( tp ) ;
            const tp_name_t* tpn = tport_name( tport );

            string transportDesc = string(tpn->tpn_proto) + "/" + tpn->tpn_host + ":" + tpn->tpn_port ;
            string contact = "<" ;
            contact.append( tport_has_tls(tport) ? "sips:" : "sip:") ;
            contact.append( tpn->tpn_host ) ;
            contact.append( ":" ) ;
            contact.append( tpn->tpn_port ) ;
            contact.append(">") ;
            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - contact header: " << contact  ;

            tport_unref( tp ) ;
    
            //create tags for headers
            tagi_t* tags = makeTags( headers, transportDesc ) ;

            if( body.length() && !searchForHeader( tags, siptag_content_type, contentType ) ) {
                if( 0 == body.find("v=0") ) {
                    contentType = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - automatically detecting content-type as application/sdp"  ;
                }
            }

            rc = nta_incoming_treply( irq, code, status
                ,TAG_IF( (sip_method_invite == sip->sip_request->rq_method || sip->sip_request->rq_method == sip_method_subscribe) &&
                    !searchForHeader( tags, siptag_contact_str, contact ), SIPTAG_CONTACT_STR(contact.c_str()) )
                ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                ,TAG_NEXT(tags)
                ,TAG_END() ) ;
            if( 0 != rc ) {
                bSentOK = false ;
                failMsg = "Unknown server error sending response" ;
                assert(false) ;
            }

            /* we must explicitly delete an object allocated with placement new */
            if( tags ) deleteTags( tags );

            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest destroying irq " << irq  ;
            bDestroyIrq = true ;                        
        }
        else if( iip ) {
            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest found invite in progress " << std::hex << iip  ;
           /* invite in progress */
            nta_leg_t* leg = iip->leg() ;
            irq = iip->irq() ;         
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;

            msg_t* msg = nta_incoming_getrequest( irq ) ;
            sip_t *sip = sip_object( msg );

            tport_t *tp = nta_incoming_transport(m_agent, irq, msg) ; 
            tport_t *tport = tport_parent( tp ) ;
            const tp_name_t* tpn = tport_name( tport );

            string transportDesc = string(tpn->tpn_proto) + "/" + tpn->tpn_host + ":" + tpn->tpn_port ;
            string contact = "<" ;
            contact.append( tport_has_tls(tport) ? "sips:" : "sip:") ;
            contact.append( tpn->tpn_host ) ;
            contact.append( ":" ) ;
            contact.append( tpn->tpn_port ) ;
            contact.append(">") ;
            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - contact header: " << contact  ;

            tport_unref( tp ) ;
    
            //create tags for headers
            tagi_t* tags = makeTags( headers, transportDesc ) ;

            dialogId = dlg->getDialogId() ;

            dlg->setSipStatus( code ) ;

            /* if the client included Require: 100rel on a provisional, send it reliably */
            bool bReliable = false ;
            if( code > 100 && code < 200 ) {
                int i = 0 ;
                while( tags[i].t_tag != tag_null ) {
                    if( tags[i].t_tag == siptag_require_str && NULL != strstr( (const char*) tags[i].t_value, "100rel") ) {
                        bReliable = true ;
                        break ;
                    }
                    i++ ;
                }
             }

             /* update local sdp if provided */
             string strLocalSdp ;
             if( !body.empty()  ) {
                dlg->setLocalSdp( body.c_str() ) ;
                string strLocalContentType ;
                if( searchForHeader( tags, siptag_content_type_str, strLocalContentType ) ) {
                    dlg->setLocalContentType( strLocalContentType ) ;
                }
                else {
                    /* set content-type if we can detect it */
                    if( 0 == body.find("v=0") ) {
                        contentType = "application/sdp" ;
                        dlg->setLocalContentType( contentType ) ;
                    }
                }
             }

             /* set session timer if required */
             if( 200 == code ) {
                string strSessionExpires ;
                if( searchForHeader( tags, siptag_session_expires_str, strSessionExpires ) ) {
                    sip_session_expires_t* se = sip_session_expires_make(m_pController->getHome(), strSessionExpires.c_str() );

                    dlg->setSessionTimer( std::max((unsigned long) 90, se->x_delta), !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::they_are_refresher : SipDialog::we_are_refresher ) ;
                    su_free( m_pController->getHome(), se ) ;
                }
             }

            /* iterate through data.opts.headers, adding headers to the response */
            if( bReliable ) {
                DR_LOG(log_debug) << "Sending " << dec << code << " response reliably"  ;
                nta_reliable_t* rel = nta_reliable_treply( irq, uasPrack, this, code, status
                    ,SIPTAG_CONTACT_STR(contact.c_str())
                    ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                    ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                    ,TAG_NEXT(tags)
                    ,TAG_END() ) ;

                if( !rel ) {
                    bSentOK = false ;
                    failMsg = "Remote endpoint does not support 100rel" ;
                    DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - failed sending reliable provisional response; most likely remote endpoint does not support 100rel"  ;
                } 
                else {
                    iip->setReliable( rel ) ;
                    addReliable( rel, iip ) ;                    
                }
            }
            else {
                DR_LOG(log_debug) << "Sending " << dec << code << " response (not reliably)  on irq " << hex << irq  ;
                rc = nta_incoming_treply( irq, code, status
                    ,TAG_IF( code >= 200 && code < 300
                    ,SIPTAG_CONTACT_STR(contact.c_str()))
                    ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                    ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                    ,TAG_NEXT(tags)
                    ,TAG_END() ) ; 
                if( 0 != rc ) {
                    DR_LOG(log_error) << "Error " << dec << rc << " sending response on irq " << hex << irq  ;
                    bSentOK = false ;
                    failMsg = "Unknown server error sending response" ;
                    assert(false) ;
                }
            }
            /* we must explicitly delete an object allocated with placement new */
            if( tags ) deleteTags( tags );
        }
        else {

            DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - unable to find irq or iip for transaction id " << transactionId  ;

            /* failed */
            string strMethod ;
            bSentOK = false ;
            failMsg = "Response not sent due to unknown transaction" ;  

            if( FindCSeqMethod( headers, strMethod ) ) {
                DR_LOG(log_debug) << "silently discarding response to " << strMethod  ;

                if( 0 == strMethod.compare("CANCEL") ) {
                    failMsg = "200 OK to incoming CANCEL is automatically generated by the stack" ;
                }
            }                     
        }

        if( bSentOK ) {
            string encodedMessage ;
            msg_t* msg = nta_incoming_getresponse( irq ) ;  // adds a ref
            EncodeStackMessage( sip_object(msg), encodedMessage ) ;
            SipMsgData_t meta( msg, irq, "application" ) ;

            string s ;
            meta.toMessageFormat(s) ;
            string data = s + "|" + transactionId + "|" + dialogId + "|" + "|Msg sent:|" + DR_CRLF + encodedMessage ;

            m_pController->getClientController()->route_api_response( clientMsgId, "OK", data) ;

            if( iip && code >= 300 ) {
                Cdr::postCdr( boost::make_shared<CdrStop>( msg, "application", Cdr::call_rejected ) );
            }
            msg_destroy(msg) ;      // release the ref          
        }
        else {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", failMsg) ;
        }
        /* tell client controller to flush transaction data on any final response to a non-INVITE */
        if( sip_method_invite != nta_incoming_method(irq) && code >= 200 ) {
            m_pController->getClientController()->removeNetTransaction( transactionId ) ;
        }
        else if( sip_method_invite == nta_incoming_method(irq) && code == 403 ) {
            //XXXX: We have a leak when someone sends us an INVITE, we reject, and they never send us an ACK
            //in that case the clientController netTransaction is kep forever, waiting for that ACK
            //long run need a better way to time those out;
            //short term, this only seems to happen from sipcli hack attempts that we respond 403 Forbidden
            //so at least for that case let's just clear the state in advance of the ACK
            m_pController->getClientController()->removeNetTransaction( transactionId ) ;            
        }

        if( bDestroyIrq ) nta_incoming_destroy(irq) ;    

        pData->~SipMessageData() ;
    }

    int SipDialogController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog: " << sip->sip_request->rq_method_name << " irq " << irq  ;
        int rc = 0 ;
        string transactionId ;
        generateUuid( transactionId ) ;

        switch (sip->sip_request->rq_method) {
            case sip_method_ack:
            {
                /* ack to 200 OK comes here  */
                boost::shared_ptr<IIP> iip ;
                boost::shared_ptr<SipDialog> dlg ;                
                if( !findIIPByLeg( leg, iip ) ) {
                    
                    /* not a new INVITE, so it should be found as an existing dialog; i.e. a reINVITE */
                    if( !findDialogByLeg( leg, dlg ) ) {
                        DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg"  ;
                        assert(0) ;
                        return -1 ;

                    }
                }
                else {
                    transactionId = iip->getTransactionId() ;

                    dlg = this->clearIIP( leg ) ;
                    addDialog( dlg ) ;
                }
                string encodedMessage ;
                msg_t* msg = nta_incoming_getrequest( irq ) ; // adds a reference
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq ) ;
                msg_destroy(msg) ;

                m_pController->getClientController()->route_ack_request_inside_dialog(  encodedMessage, meta, irq, sip, transactionId, dlg->getTransactionId(), dlg->getDialogId() ) ;

                nta_incoming_destroy(irq) ;
                break ;
            }
            default:
            {
                boost::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg"  ;
                    return 481 ;
                    assert(0) ;
                }

                /* if this is a re-INVITE deal with session timers */
                if( sip_method_invite == sip->sip_request->rq_method ) {
                    if( dlg->hasSessionTimer() ) { dlg->cancelSessionTimer() ; }

                    /* reject if session timer is too small */
                    if( sip->sip_session_expires && sip->sip_session_expires->x_delta < dlg->getMinSE() ) {
                        ostringstream o ;
                        o << dlg->getMinSE() ;
                        nta_incoming_treply( irq, SIP_422_SESSION_TIMER_TOO_SMALL, 
                            SIPTAG_MIN_SE_STR(o.str().c_str()),
                            TAG_END() ) ;  
                        return 0 ;             
                    }
                    if( sip->sip_session_expires ) {
                        dlg->setSessionTimer( sip->sip_session_expires->x_delta, 
                            !sip->sip_session_expires->x_refresher || 0 == strcmp( sip->sip_session_expires->x_refresher, "uac") ? 
                            SipDialog::they_are_refresher : 
                            SipDialog::we_are_refresher) ;
                    }

                }

                string encodedMessage ;
                msg_t* msg = nta_incoming_getrequest( irq ) ;   //adds a reference
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq ) ;
                msg_destroy( msg ); // release the reference

                bool routed = m_pController->getClientController()->route_request_inside_dialog( encodedMessage, meta, irq, sip, transactionId, dlg->getDialogId() ) ;

                addIncomingRequestTransaction( irq, transactionId) ;
    
                if( sip_method_bye == sip->sip_request->rq_method ) {
                    //clear dialog when we send a 200 OK response to BYE
                    this->clearDialog( leg ) ;
                    if( !routed ) {
                        nta_incoming_treply( irq, SIP_200_OK, TAG_END() ) ;                
                    }
                } 
            }
        }
        return rc ;
    }
    int SipDialogController::processResponseInsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: "  ;
    	ostringstream o ;
        boost::shared_ptr<RIP> rip  ;

        if( findRIPByOrq( orq, rip ) ) {
            DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: found request for response"  ;

            string encodedMessage ;
            bool truncated ;
            msg_t* msg = nta_outgoing_getresponse(orq) ;  // adds a reference
            SipMsgData_t meta( msg, orq, "network") ;

            EncodeStackMessage( sip, encodedMessage ) ;
            msg_destroy(msg) ;                             // releases reference
            
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, rip->getTransactionId(), rip->getDialogId() ) ;            

            if( sip->sip_cseq->cs_method == sip_method_bye ) {
                string dialogId = rip->getDialogId() ;
                if( dialogId.length() > 0 ) {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: clearing dialog after receiving 200 OK to BYE"  ;
                    clearDialog( dialogId ) ;
                }
                else {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: got 200 OK to BYE but don't have dialog id"  ;
                    assert(false) ;
                }
            }
            clearRIP( orq ) ;          
        }
        else {
            DR_LOG(log_error) << "SipDialogController::processResponseInsideDialog: unable to find request associated with response"  ;            
        }
        nta_outgoing_destroy( orq ) ;
  
		return 0 ;
    }
    int SipDialogController::processResponseToRefreshingReinvite( nta_outgoing_t* orq, sip_t const* sip ) {
        DR_LOG(log_debug) << "SipDialogController::processResponseToRefreshingReinvite: "  ;
        ostringstream o ;
        boost::shared_ptr<RIP> rip  ;

        nta_leg_t* leg = nta_leg_by_call_id(m_pController->getAgent(), sip->sip_call_id->i_id);
        assert(leg) ;
        boost::shared_ptr<SipDialog> dlg ;
        if( !findDialogByLeg( leg, dlg ) ) {
            assert(0) ;
        }
        if( findRIPByOrq( orq, rip ) ) {
            clearRIP( orq ) ;          

            nta_outgoing_t* ack_request = nta_outgoing_tcreate(leg, NULL, NULL, NULL,
                   SIP_METHOD_ACK,
                   (url_string_t*) sip->sip_contact->m_url ,
                   TAG_END());

            nta_outgoing_destroy( ack_request ) ;

            if( sip->sip_status->st_status != 200 ) {
                //TODO: notify client that call has failed, send BYE
            }
            else {
                /* reset session expires timer, if provided */
                sip_session_expires_t* se = sip_session_expires(sip) ;
                if( se ) {                
                    //TODO: if session-expires value is less than min-se ACK and then BYE with Reason header    
                    dlg->setSessionTimer( se->x_delta, !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::we_are_refresher : SipDialog::they_are_refresher ) ;
                }
             }
        }
        nta_outgoing_destroy( orq ) ;
        
        return 0 ;
        
    }

    int SipDialogController::processCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
        if( !sip ) {
            DR_LOG(log_debug) << "SipDialogController::processCancel called with null sip pointer"  ;
            return -1 ;
        }
        DR_LOG(log_debug) << "SipDialogController::processCancelOrAck: " << sip->sip_request->rq_method_name  ;
        string transactionId ;
        generateUuid( transactionId ) ;

        if( sip->sip_request->rq_method == sip_method_cancel ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByIrq( irq, iip ) ) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;

            if( !dlg ) {
                DR_LOG(log_error) << "No dialog exists for invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }

            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - Received CANCEL for call-id " << sip->sip_call_id->i_id << ", sending to client"  ;

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( irq ) ;   // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, irq ) ;
            msg_destroy(msg);                               // releases reference

            m_pClientController->route_request_inside_invite( encodedMessage, meta, irq, sip, iip->getTransactionId(), dlg->getDialogId() ) ;
            
            //TODO: sofia has already sent 200 OK to cancel and 487 to INVITE.  Do we need to keep this irq around?
            addIncomingRequestTransaction( irq, transactionId) ;

            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - clearing IIP "   ;
            this->clearIIP( iip->leg() ) ;
            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - done clearing IIP "   ;

        }
        else if( sip->sip_request->rq_method == sip_method_ack ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByIrq( irq, iip ) ) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for ACK with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }
            boost::shared_ptr<SipDialog> dlg = this->clearIIP( iip->leg() ) ;
            string transactionId ;
            generateUuid( transactionId ) ;

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( irq ) ;  // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, irq ) ;
            msg_destroy( msg ) ;    //release the reference

            m_pController->getClientController()->route_ack_request_inside_dialog( encodedMessage, meta, irq, sip, transactionId, dlg->getTransactionId(), dlg->getDialogId() ) ;   
            
            //NB: when we get a CANCEL sofia sends the 487 response to the INVITE itself, so our latest sip status will be a provisional
            //not sure that we need to do anything particular about that however....though it we write cdrs we would want to capture the 487 final response
        
            //another issue is that on any non-success response sent to an incoming INVITE the subsequent ACK is not sent to the client
            //because there is no dialog created and thus no routing available.  Should fix this.
        }
        else {
            DR_LOG(log_debug) << "Received " << sip->sip_request->rq_method_name << " for call-id " << sip->sip_call_id->i_id << ", discarding"  ;
        }
        return 0 ;
    }
    int SipDialogController::processPrack( nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processPrack: "  ;
        boost::shared_ptr<IIP> iip ;
        if( findIIPByReliable( rel, iip ) ) {
            string transactionId ;
            generateUuid( transactionId ) ;
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;
            assert( dlg ) ;

            m_pClientController->addDialogForTransaction( dlg->getTransactionId(), dlg->getDialogId() ) ;  

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( prack ) ; // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, prack ) ;
            msg_destroy(msg);                               // releases the reference

            m_pClientController->route_request_inside_dialog( encodedMessage, meta, prack, sip, transactionId, dlg->getDialogId() ) ;

            iip->destroyReliable() ;

            addIncomingRequestTransaction( prack, transactionId) ;
        }
        else {
            assert(0) ;
        }
        //return 200 ;
        return 0 ;
    }
    void SipDialogController::notifyRefreshDialog( boost::shared_ptr<SipDialog> dlg ) {
        nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
        if( leg ) {
            string strSdp = dlg->getLocalEndpoint().m_strSdp ;
            string strContentType = dlg->getLocalEndpoint().m_strContentType ;

            assert( dlg->getSessionExpiresSecs() ) ;
            ostringstream o,v ;
            o << dlg->getSessionExpiresSecs() << "; refresher=uac" ;
            v << dlg->getMinSE() ;

            string contact ;
            contact = "<" ;
            contact.append( ( 0 == dlg->getProtocol().compare("tls") ? "sips:" : "sip:") ) ;
            contact.append( dlg->getTransportAddress() ) ;
            contact.append( ":" ) ;
            contact.append( dlg->getTransportPort() ) ;
            contact.append(">") ;

            nta_outgoing_t* orq = nta_outgoing_tcreate( leg,  response_to_refreshing_reinvite, (nta_outgoing_magic_t *) m_pController,
                                            NULL,
                                            SIP_METHOD_INVITE,
                                            NULL,
                                            SIPTAG_SESSION_EXPIRES_STR(o.str().c_str()),
                                            SIPTAG_MIN_SE_STR(v.str().c_str()),
                                            SIPTAG_CONTACT_STR( contact.c_str() ),
                                            SIPTAG_CONTENT_TYPE_STR(strContentType.c_str()),
                                            SIPTAG_PAYLOAD_STR(strSdp.c_str()),
                                            TAG_END() ) ;
            
            string transactionId ;
            generateUuid( transactionId ) ;

            boost::shared_ptr<RIP> p = boost::make_shared<RIP>( transactionId ) ; 
            addRIP( orq, p ) ;

            //m_pClientController->route_event_inside_dialog( "{\"eventName\": \"refresh\"}",dlg->getTransactionId(), dlg->getDialogId() ) ;
        }
    }
    void SipDialogController::notifyTerminateStaleDialog( boost::shared_ptr<SipDialog> dlg ) {
        nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
        if( leg ) {
            nta_outgoing_t* orq = nta_outgoing_tcreate( leg, NULL, NULL,
                                            NULL,
                                            SIP_METHOD_BYE,
                                            NULL,
                                            SIPTAG_REASON_STR("SIP ;cause=200 ;text=\"Session timer expired\""),
                                            TAG_END() ) ;
            nta_outgoing_destroy(orq) ;
            //m_pClientController->route_event_inside_dialog( "{\"eventName\": \"terminate\",\"eventData\":\"session expired\"}",dlg->getTransactionId(), dlg->getDialogId() ) ;
        }
        clearDialog( dlg->getDialogId() ) ;
    }
    void SipDialogController::bindIrq( nta_incoming_t* irq ) {
        nta_incoming_bind( irq, uasCancelOrAck, (nta_incoming_magic_t *) m_pController ) ;
    }
    bool SipDialogController::searchForHeader( tagi_t* tags, tag_type_t header, string& value ) {
        int i = 0 ;
        while( tags[i].t_tag != tag_null ) {
            if( tags[i].t_tag == header ) {
                value.assign( (const char*) tags[i].t_value );
                return true ;
            }
            i++ ;
        }
        return false ;
    }
    void SipDialogController::addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) {
        const char* a_tag = nta_incoming_tag( irq, NULL) ;
        nta_leg_tag( leg, a_tag ) ;
        dlg->setLocalTag( a_tag ) ;

        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, irq, transactionId, dlg) ;
        m_mapIrq2IIP.insert( mapIrq2IIP::value_type(irq, p) ) ;
        m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   
        m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;   

        this->bindIrq( irq ) ;
        DR_LOG(log_debug) << "SipDialogController::addIncomingInviteTransaction:  added iip: " << hex << p << " with leg " 
            << leg << ", irq: " << irq << ", transacionId " << transactionId;
    }
    void SipDialogController::addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, boost::shared_ptr<SipDialog> dlg ) {
        DR_LOG(log_debug) << "SipDialogController::addOutgoingInviteTransaction:  adding leg " << std::hex << leg  ;
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, orq, dlg->getTransactionId(), dlg) ;
        m_mapOrq2IIP.insert( mapOrq2IIP::value_type(orq, p) ) ;
        m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(dlg->getTransactionId(), p) ) ;   
        m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;               
    }

    boost::shared_ptr<SipDialog> SipDialogController::clearIIP( nta_leg_t* leg ) {
        DR_LOG(log_debug) << "SipDialogController::clearIIP:  clearing leg " << std::hex << leg  ;
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
        assert( it != m_mapLeg2IIP.end() ) ;
        boost::shared_ptr<IIP> iip = it->second ;
        nta_incoming_t* irq = iip->irq() ;
        nta_outgoing_t* orq = iip->orq() ;
        nta_reliable_t* rel = iip->rel(); 
        boost::shared_ptr<SipDialog>  dlg = iip->dlg() ;
        mapIrq2IIP::iterator itIrq = m_mapIrq2IIP.find( iip->irq() ) ;
        mapOrq2IIP::iterator itOrq = m_mapOrq2IIP.find( iip->orq() ) ;
        mapTransactionId2IIP::iterator itTransaction = m_mapTransactionId2IIP.find( iip->getTransactionId() ) ;
        assert( itTransaction != m_mapTransactionId2IIP.end() ) ;

        assert( !(m_mapIrq2IIP.end() == itIrq && m_mapOrq2IIP.end() == itOrq )) ;

        m_mapLeg2IIP.erase( it ) ;
        if( itIrq != m_mapIrq2IIP.end() ) m_mapIrq2IIP.erase( itIrq ) ;
        if( itOrq != m_mapOrq2IIP.end() ) m_mapOrq2IIP.erase( itOrq ) ;
        m_mapTransactionId2IIP.erase( itTransaction ) ;

        if( irq ) nta_incoming_destroy( irq ) ;
        if( orq ) nta_outgoing_destroy( orq ) ;

        if( rel ) {
            mapRel2IIP::iterator itRel = m_mapRel2IIP.find( rel ) ;
            if( m_mapRel2IIP.end() != itRel ) {
                m_mapRel2IIP.erase( itRel ) ;
            }
            nta_reliable_destroy( rel ) ;
        }
        return dlg ;            
    }
    void SipDialogController::clearDialog( const string& strDialogId ) {
        boost::lock_guard<boost::mutex> lock(m_mutex) ;
        
        mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
        if( m_mapId2Dialog.end() == it ) {
            DR_LOG(log_error) << "SipDialogController::clearDialog - unable to find dialog id " << strDialogId 
                << " possibly because dialog was cleared from far end (race condition) or 408 Request Timeout to BYE"; 
            return ;
        }
        boost::shared_ptr<SipDialog> dlg = it->second ;
        nta_leg_t* leg = nta_leg_by_call_id( m_agent, dlg->getCallId().c_str() );
        m_mapId2Dialog.erase( it ) ;

        mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
        if( m_mapLeg2Dialog.end() == itLeg ) {
            assert(0) ;
            return ;
        }
        m_mapLeg2Dialog.erase( itLeg ) ;                
    }

    void SipDialogController::logStorageCount(void)  {
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        DR_LOG(log_debug) << "SipDialogController storage counts"  ;
        DR_LOG(log_debug) << "----------------------------------"  ;
        DR_LOG(log_debug) << "m_mapIrq2IIP size:                                               " << m_mapIrq2IIP.size()  ;
        DR_LOG(log_debug) << "m_mapOrq2IIP size:                                               " << m_mapOrq2IIP.size()  ;
        DR_LOG(log_debug) << "m_mapTransactionId2IIP size:                                     " << m_mapTransactionId2IIP.size()  ;
        DR_LOG(log_debug) << "m_mapLeg2Dialog size:                                            " << m_mapLeg2Dialog.size()  ;
        DR_LOG(log_debug) << "m_mapId2Dialog size:                                             " << m_mapId2Dialog.size()  ;
        DR_LOG(log_debug) << "m_mapOrq2RIP size:                                               " << m_mapOrq2RIP.size()  ;
    }

}
