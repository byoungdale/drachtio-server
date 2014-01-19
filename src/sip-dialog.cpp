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
#include <stdexcept>
#include <boost/functional/hash.hpp>

#include "sip-dialog.hpp"
#include "controller.hpp"


namespace {
    /* needed to be able to live in a boost unordered container */
    size_t hash_value( const drachtio::SipDialog& d) {
        std::size_t seed = 0;
        boost::hash_combine(seed, d.getCallId().c_str());
        boost::hash_combine(seed, d.getLocalEndpoint().m_strTag.c_str());
        boost::hash_combine(seed, d.getRemoteEndpoint().m_strTag.c_str());
        return seed;
    }

    void session_timer_handler( su_root_magic_t* magic, su_timer_t* timer, su_timer_arg_t* args) {
    	boost::weak_ptr<drachtio::SipDialog> *p = reinterpret_cast< boost::weak_ptr<drachtio::SipDialog> *>( args ) ;
    	boost::shared_ptr<drachtio::SipDialog> pDialog = p->lock() ;
    	if( pDialog ) pDialog->doSessionTimerHandling() ;
    	else assert(0) ;
    }
}

namespace drachtio {
	
	/* dialog generated by an incoming INVITE */
	SipDialog::SipDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip ) : m_type(we_are_uas), m_recentSipStatus(100), 
		m_startTime(time(NULL)), m_connectTime(0), m_endTime(0), m_releaseCause(no_release), m_refresher(no_refresher), m_timerSessionRefresh(NULL),m_ppSelf(NULL),
		m_nSessionExpiresSecs(0)
	{
		generateUuid( m_dialogId ) ;

		/* get sending address and port */
		string host ;
		unsigned int port ;
		theOneAndOnlyController->getTransactionSender( irq, host, port ) ;

		this->setSourceAddress( host ) ;
		this->setSourcePort( port ) ;


		/* get remaining values from the headers */
		if( sip->sip_call_id->i_id  ) m_strCallId = sip->sip_call_id->i_id ;
		const char*  rtag = nta_leg_get_rtag( leg )  ;
		if( rtag ) this->setRemoteTag( rtag ) ;
		if( sip->sip_payload ) this->setRemoteSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
		if( sip->sip_content_type ) {
			string hvalue ;
			parseGenericHeader( sip->sip_content_type->c_common, hvalue ) ;
			if( !hvalue.empty() ) this->setRemoteContentType( hvalue ) ;			
		}
	}

	/* dialog generated by an outgoing INVITE */
	SipDialog::SipDialog( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip ) : m_type(we_are_uac), m_recentSipStatus(0), 
		m_startTime(0), m_connectTime(0), m_endTime(0), m_releaseCause(no_release), m_refresher(no_refresher), m_timerSessionRefresh(NULL),m_ppSelf(NULL),
		m_nSessionExpiresSecs(0)
	{
		generateUuid( m_dialogId ) ;

		if( sip->sip_call_id->i_id ) m_strCallId = sip->sip_call_id->i_id ;
		const char *ltag = nta_leg_get_tag( leg ) ;
		if( ltag ) this->setLocalTag( ltag ) ;
		if( sip->sip_payload ) this->setLocalSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
		if( sip->sip_content_type ) {
			string hvalue ;
			parseGenericHeader( sip->sip_content_type->c_common, hvalue ) ;
			if( !hvalue.empty() ) this->setLocalContentType( hvalue ) ;			
		}
	}	
	SipDialog::~SipDialog() {
		DR_LOG(log_debug) << "SipDialog::~SipDialog - destroying sip dialog with call-id " << getCallId() << endl ;
		if( NULL != m_timerSessionRefresh ) {
			cancelSessionTimer() ;
			assert( m_ppSelf ) ;
		}
		if( m_ppSelf ) {
			delete m_ppSelf ;
		}

		nta_leg_t *leg = nta_leg_by_call_id( theOneAndOnlyController->getAgent(), getCallId().c_str() );
		assert( leg ) ;
		if( leg ) {
			nta_leg_destroy( leg ) ;
		}
	}

	void SipDialog::setSessionTimer( unsigned long nSecs, SessionRefresher_t whoIsResponsible ) {
		assert( NULL == m_timerSessionRefresh ) ;
		m_refresher = whoIsResponsible ;
		su_duration_t nMilliseconds = nSecs * 1000  ;
		m_nSessionExpiresSecs = nSecs ;

		DR_LOG(log_debug) << "Session expires has been set to " << nSecs << " seconds and refresher is " << (areWeRefresher() ? "us" : "them") << endl ;

		/* if we are the refresher, then we want the timer to go off halfway through the interval */
		if( areWeRefresher() ) nMilliseconds /= 2 ;
		m_timerSessionRefresh = su_timer_create( su_root_task(theOneAndOnlyController->getRoot()), nMilliseconds ) ;

		m_ppSelf = new boost::weak_ptr<SipDialog>( shared_from_this() ) ;
		su_timer_set(m_timerSessionRefresh, session_timer_handler, (su_timer_arg_t *) m_ppSelf );
	}
	void SipDialog::cancelSessionTimer() {
		assert( NULL != m_timerSessionRefresh ) ;
		su_timer_destroy( m_timerSessionRefresh ) ;
		m_timerSessionRefresh = NULL ;
		m_refresher = no_refresher ;
		m_nSessionExpiresSecs = 0 ;
	}
	void SipDialog::doSessionTimerHandling() {
		bool bWeAreRefresher = areWeRefresher()  ;
		
		if( bWeAreRefresher ) {
			//TODO: send a refreshing reINVITE, and notify the client
			DR_LOG(log_debug) << "SipDialog::doSessionTimerHandling - sending refreshing re-INVITE with call-id " << getCallId() << endl ; 
			theOneAndOnlyController->getDialogController()->notifyRefreshDialog( shared_from_this() ) ;
		}
		else {
			//TODO: tear down the leg, and notify the client
			DR_LOG(log_debug) << "SipDialog::doSessionTimerHandling - tearing down sip dialog with call-id " << getCallId() 
				<< " because remote peer did not refresh the session within the specified interval" << endl ; 
			theOneAndOnlyController->getDialogController()->notifyTerminateStaleDialog( shared_from_this() ) ;
		}

		assert( m_ppSelf ) ;
		delete m_ppSelf ; m_ppSelf = NULL ; m_timerSessionRefresh = NULL ; m_refresher = no_refresher ; m_nSessionExpiresSecs = 0 ;

	}
} 
