#include "strategy.h"
#include <fstream>
#include "kdb_function.h"


using namespace std;
using namespace kdb;

kdb::Connector kdbConnector;
string m_kdbScript;
int m_ShortLongInitNum = 0;
int m_ReadShortLongInitNum = 0;
int m_ReadShortLongSignal = 0;
TThostFtdcVolumeType m_VolumeTarget = 1;
int m_OrderVolumeMultiple = 2;
int m_IndexFuturePositionLimit = 20;
string m_StrategyType = "";
int m_TradedVolume = 0;
double m_ReadLegOneAskPrice1 = 0.0;
double m_ReadLegOneBidPrice1 = 0.0;
int m_OnTickCount = 0;

//难易程度:   buy  1,-1,0 的含义是:  在AskPrice1下调一个跳价
//           sell 1,-1,0 的含义是:  在BidPrice1上调一个跳价
//           sell -1,1,0 的含义是:  在AskPrice1下调一个跳价
//           buy  -1,1,0 的含义是:  在BidPrice1上调一个跳价
//           最后一个数字表明多少秒重新挂，0就是每个Tick都检查，大于零的数字就是过多少秒重新挂，小于0应该是错误
vector<int> orderType = {0, 0, 0};
int n = 0;

void Strategy::OnTickData(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	if (m_StrategyType == "Quote")
	{
		DataInsertToKDB(pDepthMarketData);
	}
	else
	{
		//更新TD实例中的行情信息

		//计算账户的盈亏信息
		CalculateEarningsInfo(pDepthMarketData);

		if (strcmp(pDepthMarketData->InstrumentID, m_instId) == 0)
		{
			TDSpi_stgy->Set_CThostFtdcDepthMarketDataField(pDepthMarketData);

			cerr << "策略收到行情:" << pDepthMarketData->InstrumentID << "," << pDepthMarketData->TradingDay << "," << pDepthMarketData->UpdateTime << ",最新价:" << pDepthMarketData->LastPrice << ",涨停价:" << pDepthMarketData->UpperLimitPrice << ",跌停价:" << pDepthMarketData->LowerLimitPrice << endl;

			//保存数据到vector
			SaveDataVec(pDepthMarketData);

			//保存数据到txt和csv
			//SaveDataTxtCsv(pDepthMarketData);

			//撤单追单,撤单成功后再追单
			m_TradedVolume = 0;
			TDSpi_stgy->MaintainOrder(pDepthMarketData->UpdateTime, pDepthMarketData->LastPrice, "CheckOnTick", &m_TradedVolume);

			//开仓平仓（策略主逻辑的计算）
			StrategyCalculate(pDepthMarketData);

		}
	}

	/*if (n == 0)
	{
		n++;
		TDSpi_stgy->Testkkk("CLOSE_TODAY_YD_OPEN", { -1, -5, 0 });
		TDSpi_stgy->ReqOrderInsert("sn1809", '1', "3", 146000, 1);
	}*/
	
}



//设置交易的合约代码
void Strategy::Init(string strategyType, string instId, int kdbPort, string kdbScrpit, int strategyVolumeTarget, string strategykdbscript, double par1, double par2, double par3, double par4, double par5, double par6, int strategyOrderType1, int strategyOrderType2, int strategyOrderType3, string strategyPairLeg1Symbol, string strategyPairLeg2Symbol, string strategyPairLeg3Symbol)
{
	
	m_StrategyType = strategyType;
	orderType[0] = strategyOrderType1;
	orderType[1] = strategyOrderType2;
	orderType[2] = strategyOrderType3;
	strcpy_s(m_instId, instId.c_str());
	kdbConnector.connect("localhost", kdbPort);
	m_kdbScript = strategykdbscript;
	m_VolumeTarget = strategyVolumeTarget;
	string kdb_par_string = "KindleLengthOnSecond:" + to_string((int)par1) + ";" + "WindowFrameLength:" + to_string((int)par2) + ";" + "StandardDeviationTimes:" + to_string(par3) + ";" + "PairLeg1Symbol:" + strategyPairLeg1Symbol + ";" + "PairLeg2Symbol:" + strategyPairLeg2Symbol + ";" + "PairLeg3Symbol:" + strategyPairLeg3Symbol + ";";
	string kdb_init_string = "h:hopen `::5000;PairFormula:{(x%y)};f:{x%y};bollingerBands: {[k;n;data]      movingAvg: mavg[n;data];    md: sqrt mavg[n;data*data]-movingAvg*movingAvg;      movingAvg+/:(k*-1 0 1)*\\:md};ReceiveTimeToDate:{(\"\"z\"\" $ 1970.01.01+ floor x %86400000000 )+ 08:00:00.000 +\"\"j\"\"$ 0.001* x mod  86400000000};isTable:{if[98h=type x;:1b];if[99h=type x;:98h=type key x];0b};isTable2: {@[{isTable value x}; x; 0b]}; ";
	kdbConnector.sync(kdb_init_string.c_str());
	kdbConnector.sync(kdb_par_string.c_str());
	m_kdbScript = kdbScrpit + m_kdbScript;
	////////////////////////////////////////////////////
	/////如果表格不存在设置初始信号为0             //////
	///////////////////////////////////////////////////
	kdb::Result res = kdbConnector.sync("isTable2 `ShortLong");
	if (res.res_->g)
	{
		res = kdbConnector.sync("exec count Signal from ShortLong");
		m_ShortLongInitNum = res.res_->j;

	}
	else
	{
		kdbConnector.sync("FinalSignal:([] Date:(); ReceiveDate:(); Symbol:(); LegOneBidPrice1:(); LegOneBidVol1:(); LegOneAskPrice1:(); LegOneAskVol1:(); LegTwoBidPrice1:(); LegTwoBidVol1:(); LegTwoAskPrice1:(); LegTwoAskVol1:(); LegThreeBidPrice1:(); LegThreeBidVol1:(); LegThreeAskPrice1:(); LegThreeAskVol1:(); BidPrice1:(); BidVol1:(); AskPrice1:(); AskVol:(); LowerBand:(); HigherBand:(); Signal:());ShortLong:FinalSignal;");
		kdbConnector.sync(m_kdbScript.c_str());
		m_ShortLongInitNum = 0;

	}


}



void Strategy::StrategyCalculate(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	TThostFtdcInstrumentIDType    instId;//合约,合约代码在结构体里已经有了
	strcpy_s(instId, m_instId);
	string instIDString = instId;
	string order_type = TDSpi_stgy->Send_TThostFtdcCombOffsetFlagType(instIDString.substr(0,2));
	

	TThostFtdcDirectionType       dir;//方向,'0'买，'1'卖
	TThostFtdcCombOffsetFlagType  kpp = "8";//开平，"0"开，"1"平,"3"平今
	TThostFtdcPriceType           price = 0.0;//价格，0是市价,上期所不支持
	TThostFtdcVolumeType          vol = 0;//数量

	kdbConnector.sync(m_kdbScript.c_str());
	kdb::Result res = kdbConnector.sync("exec count Signal from ShortLong");
	m_ReadShortLongInitNum = res.res_->j;
	if (m_ReadShortLongInitNum > 0)
	{
		res = kdbConnector.sync("exec last LegOneAskPrice1 from ShortLong");
		m_ReadLegOneAskPrice1 = res.res_->f;
		res = kdbConnector.sync("exec last LegOneBidPrice1 from ShortLong");
		m_ReadLegOneBidPrice1 = res.res_->f;
	}

	if (m_ReadShortLongInitNum > m_ShortLongInitNum)
	{
		m_ShortLongInitNum = m_ReadShortLongInitNum;
		kdb::Result res = kdbConnector.sync("exec last Signal from ShortLong");
		m_ReadShortLongSignal = res.res_->j;


		if (m_ReadShortLongInitNum == 1)
		{
			m_OrderVolumeMultiple = 1;
		}
		else
		{
			m_OrderVolumeMultiple = 2;
		}

		if (m_ReadShortLongSignal > 0)
		{
			dir = '0';
			price = pDepthMarketData->AskPrice1;
			vol = m_VolumeTarget * m_OrderVolumeMultiple;
			TDSpi_stgy->MaintainOrder(pDepthMarketData->UpdateTime, pDepthMarketData->LastPrice, "CancelOppositeOrder", &m_TradedVolume);
			if (vol - m_TradedVolume > 0)
			{
				TDSpi_stgy->SendOrderDecoration(instId, order_type, dir, &kpp, m_ReadLegOneAskPrice1, m_ReadLegOneBidPrice1, price, vol - m_TradedVolume, orderType, pDepthMarketData);

			}

		}
		else if (m_ReadShortLongSignal < 0)
		{
			dir = '1';
			price = pDepthMarketData->BidPrice1;
			vol = m_VolumeTarget * m_OrderVolumeMultiple;
			TDSpi_stgy->MaintainOrder(pDepthMarketData->UpdateTime, pDepthMarketData->LastPrice, "CancelOppositeOrder", &m_TradedVolume);
			if (vol - m_TradedVolume > 0)
			{
				TDSpi_stgy->SendOrderDecoration(instId, order_type, dir, &kpp, m_ReadLegOneAskPrice1, m_ReadLegOneBidPrice1, price, vol - m_TradedVolume, orderType, pDepthMarketData);
			}
			
		}
		m_TradedVolume = 0;
	}

}





//设置是否允许开仓
void Strategy::set_allow_open(bool x)
{
	m_allow_open = x;

	if(m_allow_open == true)
	{
		cerr<<"当前设置结果：允许开仓"<<endl;


	}
	else if(m_allow_open == false)
	{
		cerr<<"当前设置结果：禁止开仓"<<endl;

	}

}





//保存数据到txt和csv
void Strategy::SaveDataTxtCsv(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	//保存行情到txt
	string date = pDepthMarketData->TradingDay;
	string time = pDepthMarketData->UpdateTime;
	double buy1price = pDepthMarketData->BidPrice1;
	int buy1vol = pDepthMarketData->BidVolume1;
	double new1 = pDepthMarketData->LastPrice;
	double sell1price = pDepthMarketData->AskPrice1;
	int sell1vol = pDepthMarketData->AskVolume1;
	double vol = pDepthMarketData->Volume;
	double openinterest = pDepthMarketData->OpenInterest;//持仓量



	string instId = pDepthMarketData->InstrumentID;

	//更改date的格式
	string a = date.substr(0,4);
	string b = date.substr(4,2);
	string c = date.substr(6,2);

	string date_new = a + "-" + b + "-" + c;


	ofstream fout_data("output/" + instId + "_" + date + ".txt",ios::app);

	fout_data<<date_new<<","<<time<<","<<buy1price<<","<<buy1vol<<","<<new1<<","<<sell1price<<","<<sell1vol<<","<<vol<<","<<openinterest<<endl;

	fout_data.close();




	ofstream fout_data_csv("output/" + instId + "_" + date + ".csv",ios::app);

	fout_data_csv<<date_new<<","<<time<<","<<buy1price<<","<<buy1vol<<","<<new1<<","<<sell1price<<","<<sell1vol<<","<<vol<<","<<openinterest<<endl;

	fout_data_csv.close();

}



//保存数据到vector
void Strategy::SaveDataVec(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	m_OnTickCount++;
	if (m_OnTickCount > 120)
	{
		m_OnTickCount = 0;
		string date = pDepthMarketData->TradingDay;
		string time = pDepthMarketData->UpdateTime;
		double buy1price = pDepthMarketData->BidPrice1;
		int buy1vol = pDepthMarketData->BidVolume1;
		double new1 = pDepthMarketData->LastPrice;
		double sell1price = pDepthMarketData->AskPrice1;
		int sell1vol = pDepthMarketData->AskVolume1;
		double vol = pDepthMarketData->Volume;
		double openinterest = pDepthMarketData->OpenInterest;//持仓量


		futureData.date = date;
		futureData.time = time;
		futureData.buy1price = buy1price;
		futureData.buy1vol = buy1vol;
		futureData.new1 = new1;
		futureData.sell1price = sell1price;
		futureData.sell1vol = sell1vol;
		futureData.vol = vol;
		futureData.openinterest = openinterest;
		m_futureData_vec.push_back(futureData);
	}
	

}


void Strategy::set_instMessage_map_stgy(map<string, CThostFtdcInstrumentField*>& instMessage_map_stgy)
{
	m_instMessage_map_stgy = instMessage_map_stgy;
	cerr<<"收到合约个数:"<<m_instMessage_map_stgy.size()<<endl;

}


//计算账户的盈亏信息
void Strategy::CalculateEarningsInfo(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	//更新合约的最新价，没有持仓就不需要更新，有持仓的，不是交易的合约也要更新。要先计算盈亏信息再计算策略逻辑

	//判断该合约是否有持仓
	if(TDSpi_stgy->send_trade_message_map_KeyNum(pDepthMarketData->InstrumentID) > 0)
		TDSpi_stgy->setLastPrice(pDepthMarketData->InstrumentID, pDepthMarketData->LastPrice);


	//整个账户的浮动盈亏,按开仓价算
	m_openProfit_account = TDSpi_stgy->sendOpenProfit_account(pDepthMarketData->InstrumentID, pDepthMarketData->LastPrice); 

	//整个账户的平仓盈亏
	m_closeProfit_account = TDSpi_stgy->sendCloseProfit();

	cerr<<" 平仓盈亏:"<<m_closeProfit_account<<",浮动盈亏:"<<m_openProfit_account<<"当前合约:"<<pDepthMarketData->InstrumentID<<" 最新价:"<<pDepthMarketData->LastPrice<<" 时间:"<<pDepthMarketData->UpdateTime<<endl;//double类型为0有时候会是-1.63709e-010，是小于0的，但+1后的值是1

	TDSpi_stgy->printTrade_message_map();



}

void Strategy::GetHistoryData()
{
	vector<string> data_fileName_vec;
	Store_fileName("input/历史K线数据", data_fileName_vec);

	cout<<"历史K线文本数:"<<data_fileName_vec.size()<<endl;

	for(vector<string>::iterator iter = data_fileName_vec.begin(); iter != data_fileName_vec.end(); iter++)
	{
		cout<<*iter<<endl;
		ReadDatas(*iter, history_data_vec);

	}

	cout<<"K线条数:"<<history_data_vec.size()<<endl;

}
//存数据到kdb
void Strategy::DataInsertToKDB(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	string insertstring;
	string date = pDepthMarketData->TradingDay;
	string datesplit = "D";
	string time = pDepthMarketData->UpdateTime;
	string receivedate = return_current_time_and_date();
	string symbol = pDepthMarketData->InstrumentID;
	double bidPrice1 = pDepthMarketData->BidPrice1;
	int    bidvol1 = 1;
	double askPrice1 = pDepthMarketData->AskPrice1;
	int    askvol1 = 1;

	insertstring.append("`Quote insert (");
	insertstring.append(date.substr(0, 4) + ".");
	insertstring.append(date.substr(4, 2) + ".");
	insertstring.append(date.substr(6, 2));
	insertstring.append(datesplit);
	insertstring.append(time);
	insertstring.append(";");
	insertstring.append(receivedate);
	insertstring.append(";");
	insertstring.append("`");
	insertstring.append(symbol);
	insertstring.append(";");
	insertstring.append(to_string(bidPrice1));
	insertstring.append(";");
	insertstring.append(to_string(bidvol1));
	insertstring.append(";");
	insertstring.append(to_string(askPrice1));
	insertstring.append(";");
	insertstring.append(to_string(askvol1));
	insertstring.append(")");
	
	kdbConnector.sync(insertstring.c_str());
}

string  Strategy::return_current_time_and_date()
{
	auto now = std::chrono::system_clock::now();
	auto in_time_t = std::chrono::system_clock::to_time_t(now);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&in_time_t), "%Y.%m.%dD%X");
	auto duration = now.time_since_epoch();

	typedef std::chrono::duration<int, std::ratio_multiply<std::chrono::hours::period, std::ratio<8>
	>::type> Days; /* UTC: +8:00 */
	Days days = std::chrono::duration_cast<Days>(duration);
	duration -= days;
	auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
	duration -= hours;
	auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
	duration -= minutes;
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
	duration -= seconds;
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
	duration -= milliseconds;
	auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
	duration -= microseconds;
	auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
	cout << (ss.str()).append(".").append(to_string(milliseconds.count())) << endl;
	return (ss.str()).append(".").append(to_string(milliseconds.count()));
}
