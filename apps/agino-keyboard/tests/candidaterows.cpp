#include "candidaterows.h"
#include <QFontMetricsF>
#include <QTextLayout>
QVariant CandidateRows::data(const QModelIndex &i,int role)const{if(!i.isValid()||i.row()<0||i.row()>=rows_.size())return {};return role==Entries?QVariant(rows_[i.row()].entries):role==RowHeight?QVariant(rows_[i.row()].height):QVariant();}
void CandidateRows::setCandidates(const QStringList &s){if(candidates_!=s){candidates_=s;rebuild();}}
void CandidateRows::setAvailableWidth(qreal w){w=qMax(48.,w);if(width_!=w){width_=w;rebuild();}}
void CandidateRows::setFont(const QFont &f){if(font_!=f){font_=f;rebuild();}}
void CandidateRows::rebuild(){
 beginResetModel();rows_.clear();QFontMetricsF metrics(font_);Row row;qreal x=0;
 for(int n=0;n<candidates_.size();++n){const auto &text=candidates_[n];qreal natural=qMax(48.,metrics.horizontalAdvance(text)+40);qreal width=qMin(width_,natural),height=48;
  if(natural>width_){QTextLayout l(text,font_);l.beginLayout();qreal y=0;while(true){auto line=l.createLine();if(!line.isValid())break;line.setLineWidth(qMax(8.,width-40));y+=line.height();}l.endLayout();height=qMax(48.,y+16);}
  if(x&&x+width>width_){rows_.push_back(row);row=Row{};x=0;}
  row.entries.push_back(QVariantMap{{"text",text},{"index",n},{"x",x},{"width",width},{"height",height}});row.height=qMax(row.height,height);x+=width+8;
 }
 if(!row.entries.isEmpty())rows_.push_back(row);endResetModel();emit layoutChanged();
}
