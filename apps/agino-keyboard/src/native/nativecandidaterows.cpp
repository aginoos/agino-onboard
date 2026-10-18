// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativecandidaterows.h"
#include <QFontMetricsF>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

int NativeCandidateRows::rowCount(const QModelIndex &parent)const
{
 return parent.isValid()?0:rows_.size();
}

QVariant NativeCandidateRows::data(const QModelIndex &index,int role)const
{
 if(!index.isValid()||index.model()!=this||index.column()!=0||index.row()<0||index.row()>=rows_.size())return {};
 if(role==Entries)return rows_[index.row()].entries;
 if(role==RowHeight)return rows_[index.row()].height;
 return {};
}

QHash<int,QByteArray> NativeCandidateRows::roleNames()const
{
 return {{Entries,"entries"},{RowHeight,"rowHeight"}};
}

void NativeCandidateRows::setCandidates(const QStringList &values)
{
 if(candidates_==values)return;
 candidates_=values;
 invalidate();
}

void NativeCandidateRows::setAvailableWidth(qreal value)
{
 value=qMax(48.,value);
 if(width_==value)return;
 width_=value;
 invalidate();
}

void NativeCandidateRows::setFont(const QFont &value)
{
 if(font_==value)return;
 font_=value;
 invalidate();
}

void NativeCandidateRows::invalidate()
{
 beginResetModel();
 rows_.clear();next_=0;extent_=0;pending_=false;lookahead_=Entry{};
 endResetModel();
 emit layoutChanged();
}

NativeCandidateRows::Entry NativeCandidateRows::measure(int index)const
{
 // Keep this calculation identical to CandidateRows::rebuild. In particular,
 // do not replace horizontalAdvance or change QTextLayout's default wrapping.
 Entry entry;entry.text=candidates_[index];entry.index=index;
 QFontMetricsF metrics(font_);
 const qreal natural=qMax(48.,metrics.horizontalAdvance(entry.text)+40);
 entry.width=qMin(width_,natural);
 if(natural>width_){
  QTextLayout layout(entry.text,font_);layout.beginLayout();qreal height=0;
  while(true){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(qMax(8.,entry.width-40));height+=line.height();}
  layout.endLayout();entry.height=qMax(48.,height+16);
 }
 return entry;
}

void NativeCandidateRows::appendRow()
{
 if(complete())return;
 Row row;qreal x=0;
 while(next_<candidates_.size()){
  // Every entry is at least 48px wide. If no entry can fit, finalize
  // without shaping even a lookahead glyph from the following row.
  if(x&&x+48>width_)break;
  if(!pending_){lookahead_=measure(next_);pending_=true;}
  const Entry &entry=lookahead_;
  if(x&&x+entry.width>width_)break;
  row.entries.push_back(QVariantMap{{"text",entry.text},{"index",entry.index},{"x",x},{"width",entry.width},{"height",entry.height}});
  row.height=qMax(row.height,entry.height);
  x+=entry.width+8;
  ++next_;pending_=false;
 }
 // Every candidate is clamped to availableWidth, so an empty row cannot occur.
 row.bottom=extent_+row.height;
 const int index=rows_.size();beginInsertRows({},index,index);
 rows_.push_back(std::move(row));extent_=rows_.last().bottom+8;
 endInsertRows();
}

void NativeCandidateRows::ensureLayout(qreal bottom)
{
 if(complete())return;
 if(!std::isfinite(bottom))return;
 bottom=qMax(0.,bottom);
 const int previous=rows_.size();
 // A row can be taller than the viewport. Stop by actual row bottoms rather
 // than an assumed 48px height, including when bottom falls in an 8px gap.
 while(!complete()&&(rows_.isEmpty()||rows_.last().bottom<bottom))appendRow();
 const auto covering=std::lower_bound(rows_.cbegin(),rows_.cend(),bottom,[](const Row &row,qreal y){return row.bottom<y;});
 if(covering!=rows_.cend()){
  const int needed=int(covering-rows_.cbegin())+2;
  while(!complete()&&rows_.size()<needed)appendRow();
 }
 if(rows_.size()!=previous)emit layoutChanged();
}

qreal NativeCandidateRows::contentHeight()const
{
 if(candidates_.isEmpty())return 0;
 const qreal known=qMax(0.,extent_-8);
 if(complete())return known;
 // Estimation never inspects text or font metrics. A first estimate assumes
 // minimum-width entries; later estimates use observed entries/row and height.
 const qreal perRow=rows_.isEmpty()?qMax(1.,std::floor((width_+8)/56.)):qreal(next_)/rows_.size();
 const qreal remaining=std::ceil((candidates_.size()-next_)/perRow);
 const qreal average=rows_.isEmpty()?48.:extent_/rows_.size()-8;
 return rows_.isEmpty()?qMax(0.,remaining*56-8):known+remaining*(average+8);
}
